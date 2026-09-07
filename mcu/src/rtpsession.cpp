#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/poll.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <netdb.h>
#include <limits.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <srtp2/srtp.h>
#include <time.h>
#include "log.h"
#include "tools.h"
#include "medkit/codecs.h"
#include "rtp.h"
#include "rtpsession.h"
#include "rtpsessionset.h"
#include "ipaddress.h"
#include "medkit/stunmessage.h"
extern "C" {
#include <libavutil/base64.h>
}
#include <openssl/ossl_typ.h>

BYTE rtpEmpty[] = {0x80,0x14,0x00,0x00,0x00,0x00,0x00,0x00};

//P6 : amorçage NAT symétrique (comedia). Nombre de paquets RTP valides émis en
//rafale dès que la destination est connue, et intervalle (ms) entre eux. Plusieurs
//paquets espacés résistent à la perte UDP et fiabilisent le latch d'un pair
//symétrique (Asterisk nat=yes) qui n'émet qu'après avoir reçu du média.
#define NAT_PRIMING_BURST	3
#define NAT_PRIMING_INTERVAL_MS	20

//Durée (ms) pendant laquelle la map de réception PRÉCÉDENTE reste utilisable après
//une renégociation. Elle couvre le trou de l'offre/réponse (RFC 3264 §8) : le pair
//qui a offert continue d'émettre sous son ancienne numérotation jusqu'à recevoir la
//réponse — un aller-retour SIP, plus le temps que met un B2BUA à obtenir celle de
//l'autre jambe. Quelques secondes couvrent largement le cas ; au-delà, un pair qui
//n'a toujours pas basculé a un vrai problème et ses paquets redeviennent des rebuts.
#define RTP_MAP_FALLBACK_MS	5000

//Cadence maximale (ms) des traces « payload type non négocié ». Le régime normal
//qu'elles décrivent — l'offreur qui émet encore avec l'ancienne numérotation
//pendant une renégociation — dure quelques centaines de millisecondes à 20-50
//paquets/s : au-delà de la première ligne, ce qui compte est COMBIEN et jusqu'à
//QUAND, pas une ligne par paquet. Cf. RTPSession::OnUnknownPayloadType.
#define UNKNOWN_PT_LOG_PERIOD_MS	2000

//srtp library initializers
class SRTPLib
{
public:
	SRTPLib()	{ srtp_init();		}
	~SRTPLib()	{ srtp_shutdown();	}
};
SRTPLib srtp;

DWORD RTPSession::minLocalPort = 49152;

/***********************************
* V4Address
*	Passerelle entre l'etat d'adressage encore IPv4 de RTPSession (in_addr_t, en
*	ordre reseau) et le type d'adresse commun. Elle disparaitra avec lui a
*	l'etape 5 du chantier IPv6 : d'ici la, elle evite de
*	dupliquer une deuxieme fois la connaissance des plages d'adresses.
***********************************/
static IPAddress V4Address(in_addr_t addr)
{
	sockaddr_in sa;
	memset(&sa,0,sizeof(sa));
	sa.sin_family      = AF_INET;
	sa.sin_addr.s_addr = addr;
	return IPAddress::FromSockaddr((const sockaddr*)&sa);
}

DWORD RTPSession::maxLocalPort = 65535;

bool RTPSession::SetPortRange(int minPort, int maxPort)
{
	// mitPort should be even
	if ( minPort % 2 )
		minPort++;

	//Check port range is possitive
	if (maxPort<minPort)
		//Error
		return Error("-RTPSession port range invalid [%d,%d]\n",minPort,maxPort);

	//check min range ports
	if (maxPort-minPort<50)
	{
		//Error
		Error("-RTPSession port range too short %d, should be at least 50\n",maxPort-minPort);
		//Correct
		maxPort = minPort+50;
	}

	//check min range
	if (minPort<1024)
	{
		//Error
		Error("-RTPSession min rtp port is inside privileged range, increasing it\n");
		//Correct it
		minPort = 1024;
	}

	//Check max port
	if (maxPort>65535)
	{
		//Error
		Error("-RTPSession max rtp port is too high, decreasing it\n");
		//Correc it
		maxPort = 65535;
	}
	
	//Set range
	minLocalPort = minPort;
	maxLocalPort = maxPort;

	//Log
	Log("-RTPSession configured RTP/RTCP ports range [%d,%d]\n", minLocalPort, maxLocalPort);

	//OK
	return true;
}

std::string RTPSession::announcedIp;
bool RTPSession::announcedIpResolved = false;
std::mutex RTPSession::announcedIpMutex;

//Auto-détection de l'adresse à annoncer, à défaut de --public-ip : première
//adresse ANNONÇABLE de l'hôte. Déplacée ici depuis Endpoint::GetMediaCandidates,
//qui la refaisait — résolution comprise, et sans verrou — à chaque appel.
//
//`gethostbyname` a disparu au profit d'`IPAddress::Resolve` : il ne rendait que
//des enregistrements A et rejetait explicitement h_addrtype != AF_INET, donc un
//hôte dont le nom ne porte qu'un AAAA était invisible — et le serveur refusait
//alors de démarrer. La préférence reste à l'IPv4 (comportement historique : un
//hôte double pile annonce la même adresse qu'avant), l'IPv6 servant de repli.
static std::string DetectAnnouncedIp()
{
	char hostname[HOST_NAME_MAX];

	//Nom de l'hôte
	if (gethostname(hostname, sizeof hostname) != 0)
	{
		//Erreur
		Error("-RTPSession cannot get hostname to detect the announced IP\n");
		//Rien
		return std::string();
	}

	int err = 0;
	//Résolution ordonnée : v4 annonçables d'abord, puis v6. L'ordre est le
	//NÔTRE et pas celui du résolveur — cette adresse finit dans une ligne c=.
	const std::list<IPAddress> addrs = IPAddress::Resolve(hostname,err,AF_INET);

	if (addrs.empty())
	{
		//Erreur
		Error("-RTPSession cannot resolve \"%s\" to an announceable address (err %d)\n",hostname,err);
		//Rien
		return std::string();
	}

	//Forme canonique (RFC 5952 en v6) : deux écritures de la même adresse
	//doivent produire la même chaîne, c'est elle que verra le pair.
	return addrs.front().ToString();
}

bool RTPSession::SetAnnouncedIp(const char* ip)
{
	//Rien de fourni : l'auto-détection de GetAnnouncedIp reste en place
	if (!ip || !*ip)
		return false;

	//Littéral (v4 ou v6) OU nom d'hôte : sur une machine double pile, donner un
	//nom est la seule façon de laisser le résolveur trancher. Un nom qui ne rend
	//rien d'annonçable est un refus, au même titre qu'un littéral loopback.
	int err = 0;
	const std::list<IPAddress> addrs = IPAddress::Resolve(ip,err,AF_INET);

	if (addrs.empty())
		return Error("-RTPSession announced IP \"%s\" is not a usable address\n",ip);

	const IPAddress addr = addrs.front();

	//Une adresse annoncée fausse produit un SDP que le pair ne peut pas joindre.
	//Loopback, multicast, non spécifiée, link-local : refus plutôt qu'annonce.
	if (!addr.IsAnnounceable())
		return Error("-RTPSession announced IP \"%s\" cannot be announced (loopback, multicast, link-local or unspecified)\n",ip);

	//Verrou
	std::lock_guard<std::mutex> lock(announcedIpMutex);

	//Set : une adresse explicite dispense de toute auto-détection. On stocke la
	//forme CANONIQUE — minuscules et compression RFC 5952 en v6, forme v4 pour
	//une ::ffff:a.b.c.d — pas la chaîne d'entrée : le pair doit voir une
	//écriture stable, et le contrôleur pouvoir la comparer.
	announcedIp = addr.ToString();
	announcedIpResolved = true;

	//Log
	Log("-RTPSession announced IP set to \"%s\"\n",announcedIp.c_str());

	//OK
	return true;
}

const char* RTPSession::GetAnnouncedIp()
{
	//Verrou
	std::lock_guard<std::mutex> lock(announcedIpMutex);

	//Résolue une fois pour toutes — le pointeur rendu reste donc valide — et
	//l'échec est mémorisé au même titre que le succès : sans quoi un hôte qui ne
	//se résout pas relancerait un gethostbyname perdant à chaque appel.
	if (!announcedIpResolved)
	{
		announcedIp = DetectAnnouncedIp();
		announcedIpResolved = true;
	}

	//La chaîne, vide si l'hôte ne se résout pas
	return announcedIp.c_str();
}

/*************************
* RTPSession
* 	Constructro
**************************/
RTPSession::RTPSession(MediaFrame::Type media,Listener *listener,MediaFrame::MediaRole role) : dtls(*this)
{
	//Store listener
	this->listener = listener;
	//And media
	this->media = media;
	//Bornes de l'estimateur d'émission vidéo : cf. rtpsession.h.
	if (media == MediaFrame::Video)
		senderBWE.SetMinMaxBitrate(VideoSenderEstimateMinBps, VideoSenderEstimateMaxBps);
	this->role	= role;
	//Init values
	sendType = -1;
	simSocket = FD_INVALID;
	simRtcpSocket = FD_INVALID;
	simPort = 0;
	simRtcpPort = 0;
	sendSeq = 0;
	sendTime = random();
	sendLastTime = sendTime;
	sendSSRC = random();
	sendSR = 0;
	recSR = 0;
	sendCycles = 0;
	
	recIP = IPAddress();
	recPort = 0;
	//P7 : rattrapage NAT désactivé par défaut — le plan de contrôle l'active par la
	//propriété "natLatch" (ou en passant 0.0.0.0 à SetRemotePort), lui seul sachant
	//de quel type de jambe il s'agit. Aucun appelant historique n'est donc affecté.
	natLatch = false;
	natCorrected = false;
	natRtcpCorrected = false;
	firReqNum = 0;
	requestFPU = false;
	pendingTMBR = false;
	pendingTMBBitrate = 0;
	//Not muxing
	muxRTCP = false;
	//Default cname
	cname = strdup("default@localhost");
	//Empty types by defauilt
	rtpMapIn = NULL;
	rtpMapOut = NULL;
	//Aucune renégociation encore vue : pas de map précédente à quoi se rattraper
	rtpMapInPrev = NULL;
	setZeroTime(&rtpMapInPrevSince);
	salvagedType = 0;
	salvagedCount = 0;
	setZeroTime(&salvagedLast);
	//Aucun paquet au payload type inconnu encore reçu (cf. OnUnknownPayloadType)
	unknownPtType = 0;
	unknownPtCount = 0;
	setZeroTime(&unknownPtLast);
	//statistics
	totalSendBytes = 0;
	numSendPackets = 0;
	//Watchdog d'inactivité RTP désactivé par défaut (gap 5) : ni configuré ni armé
	setZeroTime(&lastRecv);
	rtpTimeout = 0;
	rtpTimeoutArmed = false;
	rtpTimedOut = false;
	//No reports
	setZeroTime(&lastSR);
	setZeroTime(&lastReceivedSR);
	rtt = 0;
	//No cripto
	encript = false;
	decript = false;
	//P2 : handshake DTLS client non amorcé
	dtlsClientStarted = false;
	dtlsClientFailed  = false;
	setZeroTime(&dtlsClientStart);
	//Aucun consommateur de données applicatives DTLS : le DTLS ne sert alors qu'à
	//dériver les clés SRTP, comme avant.
	appListener = NULL;
	setZeroTime(&lastAppTick);
	sendSRTPSession = NULL;
	recvSRTPSession = NULL;
	recvSRTPSession_secondary = NULL;
	recvSRTPSessionRTX = NULL;
	recvSRTPSessionRTX_secondary = NULL;
	sendKey		= NULL;
	recvKey		= NULL;
	//No ice
	iceLocalUsername = NULL;
	iceLocalPwd = NULL;
	iceRemoteUsername = NULL;
	iceRemotePwd = NULL;
	//Aucun candidat ICE distant retenu (gap 1)
	iceRemotePriority = 0;
	//P3 : aucun check STUN sortant émis, connectivité non confirmée
	iceConnected    = false;
	iceCheckStarted = false;
	iceCheckRto     = 0;
	setZeroTime(&iceLastCheck);
	memset(iceCheckTransId,0,sizeof(iceCheckTransId));
	//La cible d'envoi n'a encore été posée par personne
	iceOwnsSendAddr = false;
	iceMissingLocalPwdReported = false;
	//P5 : événement « média établi » pas encore émis
	rtpReceivedNotified = false;
	//P6 : aucune rafale d'amorçage NAT en cours
	natPrimingLeft = 0;
	setZeroTime(&natPrimingLast);
	//NO FEC
	useFEC = false;
	useNACK = false;
	useAbsTime = false;
	useTransportCC = false;
	transportSeqNum = 0;
	transportFeedbackStarted = false;
	absSendTimeExtId = 0;
	transportCCExtId = 0;
	isNACKEnabled = false;
	//Fill with 0
	memset(sendPacket,0,MTU+SRTP_MAX_TRAILER_LEN);
	//Preparamos las direcciones de envio
	sendAddr     = IPEndpoint();
	sendRtcpAddr = IPEndpoint();
	//Pas encore inscrite dans un reacteur
	running = false;
	pollGroup = NULL;
	//No stimator
	remoteRateEstimator = NULL;
	//Aucun feedback tant que la négociation n'en a pas demandé (arbitrage A2 du
	//plan) : un pair AVP strict ne doit pas recevoir d'AVPF.
	bitrateFeedbackMode = BitrateFeedbackNone;

	//Set family
	//Sockets media : dual-stack par defaut (AF_INET6 + IPV6_V6ONLY=0), sauf
	//adresse de bind v4 imposee par un profil. Init fixe la valeur definitive.
	socketFamily = AF_INET6;
	addressProfile = AddressProfiles::Default();
	addressProfileSet = false;
	
	defaultStream = NULL;
	
	useOriSeqNum	=false;
	useOriTS		=false;
	useExtFIR		=false;
	useRtcpFIR		=true;
	
	lastSendSSRC		=0;
}

/*************************
* ~RTPSession
* 	Destructor
**************************/
RTPSession::~RTPSession()
{
	//Se desinscrire AVANT tout demontage : l'estimateur est partage par les
	//jambes d'un meme Endpoint, donc le thread RTP d'une autre jambe peut nous
	//notifier. RemoveListener attend la fin d'une notification en vol (verrou
	//lecteur, cf. RemoteRateEstimator::Update) : au retour, plus personne ne
	//tient cette session, et End() peut demonter.
	if (remoteRateEstimator)
		remoteRateEstimator->RemoveListener(this);
    End();
	if (rtpMapIn)
		delete(rtpMapIn);
	if (rtpMapInPrev)
		delete(rtpMapInPrev);
	if (rtpMapOut)
		delete(rtpMapOut);
	//Delete packets
	for(Streams::iterator it=streams.begin(); it!=streams.end(); it++)
	{
		if (it->second)
			it->second->Clear();
	}	
	//Clean mem
	if (iceLocalUsername)
		free(iceLocalUsername);
	if (iceLocalPwd)
		free(iceLocalPwd);
	if (iceRemoteUsername)
		free(iceRemoteUsername);
	if (iceRemotePwd)
		free(iceRemotePwd);
	//If secure
	if (sendSRTPSession)
		//Dealoacate
		srtp_dealloc(sendSRTPSession);
	//If secure
	if (recvSRTPSession)
		//Dealoacate
		srtp_dealloc(recvSRTPSession);
	if (recvSRTPSession_secondary)
		//Dealoacate
		srtp_dealloc(recvSRTPSession_secondary);	
	//If RTX
	if (recvSRTPSessionRTX)
		//Dealoacate
		srtp_dealloc(recvSRTPSessionRTX);
	if (recvSRTPSessionRTX_secondary)
		//Dealoacate
		srtp_dealloc(recvSRTPSessionRTX_secondary);
	if (sendKey)
		free(sendKey);
	if (recvKey)
		free(recvKey);
	if (cname)
		free(cname);
	//Delete rtx packets

}

void RTPSession::SetSendingRTPMap(RTPMap &map)
{
	//If we already have one
	if (rtpMapOut)
		//Delete it
		delete(rtpMapOut);
	//Clone it
	rtpMapOut = new RTPMap(map);
}

int RTPSession::SetLocalCryptoSDES(const char* suite, const BYTE* key,const DWORD len)
{
	srtp_err_status_t err;
	srtp_policy_t policy;

	Log("-Set local RTP SDES [suite:%s,keyLen:%u]\n",suite,len);

	//empty policy
	memset(&policy, 0, sizeof(srtp_policy_t));

	//Get cypher
	if (strcmp(suite,"AES_CM_128_HMAC_SHA1_80")==0)
	{
		Log("RTPSession::SetLocalCryptoSDES() | suite: AES_CM_128_HMAC_SHA1_80\n");
		srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtp);
		srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);
	}
	else if (strcmp(suite,"AES_CM_128_HMAC_SHA1_32")==0) 
	{
		Log("RTPSession::SetLocalCryptoSDES() | suite: AES_CM_128_HMAC_SHA1_32\n");
		srtp_crypto_policy_set_aes_cm_128_hmac_sha1_32(&policy.rtp);
		srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);
	} 
	else if (strcmp(suite,"AES_CM_128_NULL_AUTH")==0)
	{
		Log("RTPSession::SetLocalCryptoSDES() | suite: AES_CM_128_NULL_AUTH\n");
		srtp_crypto_policy_set_aes_cm_128_null_auth(&policy.rtp);
		srtp_crypto_policy_set_aes_cm_128_null_auth(&policy.rtcp);
	}
	else if (strcmp(suite,"NULL_CIPHER_HMAC_SHA1_80")==0) 
	{
		Log("RTPSession::SetLocalCryptoSDES() | suite: NULL_CIPHER_HMAC_SHA1_80\n");
		srtp_crypto_policy_set_null_cipher_hmac_sha1_80(&policy.rtp);
		srtp_crypto_policy_set_null_cipher_hmac_sha1_80(&policy.rtcp);
	}
	else {
		return Error("Unknown cipher suite");
	}

	//Check sizes
	if (len!=policy.rtp.cipher_key_len)
		//Error
		return Error("Key size (%d) doesn't match the selected srtp profile (required %d)\n",len,policy.rtp.cipher_key_len);

	//Set polciy values
	policy.ssrc.type	= ssrc_any_outbound;
	policy.ssrc.value	= 0;
	policy.allow_repeat_tx  = 1; // supporte par libsrtp2 (necessaire pour le RTX)
    policy.key		= (BYTE*)key;
	policy.next		= NULL;
	srtp_t session;
	err = srtp_create(&session,&policy);	
	//Check error
	if (err!=srtp_err_status_ok)
		//Error
		return Error("Failed to create srtp session (%d)\n", err);

	//Set send SSRTP sesion
	sendSRTPSession = session;

	//Request an intra to start clean
	if (auto l = LockListener())
		//Request a I frame
		l->onFPURequested(this);

	//Evrything ok
	return 1;
}

int RTPSession::SetLocalCryptoSDES(const char* suite, const char* key64)
	//Log
{
	Log("-SetLocalCryptoSDES [key:%s,suite:%s]\n",key64,suite);

	//encript
	encript = true;

	//Get lenght
	WORD len64 = strlen(key64);
	//Allocate memory for the key
	BYTE sendKey[len64];
	//Decode
	WORD len = av_base64_decode(sendKey,key64,len64);

	//Set it
	return SetLocalCryptoSDES(suite,sendKey,len);
}

int RTPSession::SetProperties(const Properties& properties)
{
	mutex.lock();
	//Clean txtension map
	extMap.clear();
	//Le dialecte de feedback tel qu'il est aujourd'hui : une renégociation qui
	//ne reparle pas de TMMBR/REMB ne le retire pas.
	bool askedTMMBR = (bitrateFeedbackMode == BitrateFeedbackTMMBR);
	bool askedREMB  = (bitrateFeedbackMode == BitrateFeedbackREMB);
	//For each property
	for (Properties::const_iterator it=properties.begin();it!=properties.end();++it)
	{
		Log("Setting RTP property [%s:%s] on %s %s stream %p\n",it->first.c_str(),it->second.c_str(),
		    MediaFrame::RoleToString(role), MediaFrame::TypeToString(media),this);
		
		//Check
		if (it->first.compare("rtcp-mux")==0)
		{
			//Set rtcp muxing
			muxRTCP = atoi(it->second.c_str());
		} 
		else if (it->first.compare("natLatch")==0)
		{
			//Autorise le rattrapage de la cible d'envoi vers la source réellement
			//observée quand le pair a annoncé une adresse privée (NAT symétrique).
			//Désactivé par défaut : c'est au plan de contrôle, qui seul sait de quel
			//type de jambe il s'agit, de l'activer. Voir SendPacket / NatCorrectable.
			natLatch = atoi(it->second.c_str());
			if (natLatch) Log("Activated symmetric NAT latching on %s stream %p.\n", MediaFrame::TypeToString(media), this);
		}
		else if (it->first.compare("tmmbr")==0)
		{
			//TMMBR prime sur REMB : c'est la version normalisée (RFC 5104), et
			//le mode émet les deux. La résolution se fait APRÈS la boucle —
			//l'ordre d'itération d'un Properties ne se présume pas.
			askedTMMBR = atoi(it->second.c_str());
		}
		else if (it->first.compare("remb")==0)
		{
			//Le dialecte des navigateurs : ils offrent "goog-remb" et pas
			//"ccm tmmbr". Sans cette propriété, rien ne partait jamais vers eux.
			askedREMB = atoi(it->second.c_str());
		}
		else if (it->first.compare("ssrc")==0) {
			//Set ssrc for sending
			sendSSRC = atoi(it->second.c_str());
		} else if (it->first.compare("cname")==0) {
			//Check if already got one
			if (cname)
				//Delete it
				free(cname);
			//Clone
			cname = strdup(it->second.c_str());
		} else if (it->first.compare("useFEC")==0) {
			//Set fec decoding
			useFEC = atoi(it->second.c_str());
		} else if (it->first.compare("useNACK")==0) {
			//Set fec decoding
			useNACK = atoi(it->second.c_str());
			//Enable NACK until first RTT
			isNACKEnabled = useNACK;

		}
		else if (it->first.compare("useOriSeqNum")==0) {
			//Set numerotation of seqNum
			useOriSeqNum 	= atoi(it->second.c_str());
			useOriTS 	= atoi(it->second.c_str());
		}
		else if (it->first.compare("useExtFIR")==0) {
			//Set use of SIP INFO FIR
			useExtFIR = atoi(it->second.c_str());
			
		}
		else if (it->first.compare("useRtcpFIR")==0) {
			//Set use of RTCP FIR
			useRtcpFIR = atoi(it->second.c_str());

		}
		else if (it->first.compare("rtpTimeout")==0) {
			//Pré-configure UNIQUEMENT le seuil d'inactivité RTP en ms (gap 5).
			//N'arme PAS le watchdog : le chrono ne démarre qu'à l'armement explicite
			//(ArmRTPTimeout, appelé au SDP answer). Cela évite qu'un réglage posé au
			//setup ne déclenche des timeouts pendant la sonnerie.
			rtpTimeout = (DWORD)atoi(it->second.c_str());
			Log("Set rtpTimeout=%u ms (config, non armé) on %s stream %p\n",rtpTimeout,MediaFrame::TypeToString(media),this);
		}
		else if (it->first.compare(0, 5, "codec")==0) {
			// Ignore codec props
		} else if (it->first.compare("urn:ietf:params:rtp-hdrext:toffset")==0) {
			//Set extension
			extMap[atoi(it->second.c_str())] = RTPPacket::HeaderExtension::TimeOffset;
		} else if (it->first.compare("http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time")==0) {
			//Set extension
			absSendTimeExtId = (BYTE)atoi(it->second.c_str());
			extMap[absSendTimeExtId] = RTPPacket::HeaderExtension::AbsoluteSendTime;
			//Use timestamsp
			useAbsTime = true;
		} else if (it->first.compare("http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01")==0) {
			//Set extension
			transportCCExtId = (BYTE)atoi(it->second.c_str());
			extMap[transportCCExtId] = RTPPacket::HeaderExtension::TransportWideCC;
			useTransportCC = true;
		} else {
			Error("Unknown RTP property [%s]\n",it->first.c_str());
		}
	}
	//Résolution du dialecte, hors boucle : TMMBR l'emporte quel que soit l'ordre
	//dans lequel les deux propriétés sont arrivées.
	bitrateFeedbackMode = askedTMMBR ? BitrateFeedbackTMMBR
			    : askedREMB  ? BitrateFeedbackREMB
					 : BitrateFeedbackNone;
	//Ce qui mérite une annonce dépend du dialecte : un pair TMMBR (Linphone)
	//reconfigure son encodeur à chaque valeur différente, un pair REMB
	//(navigateur) attend une annonce périodique (cf. rembthrottler.h).
	bitrateFeedbackThrottler.SetPolicy(bitrateFeedbackMode == BitrateFeedbackTMMBR
					   ? RembThrottler::TmmbrPolicy : RembThrottler::RembPolicy);
	if (bitrateFeedbackMode != BitrateFeedbackNone)
		Log("Activated %s bitrate feedback on %s stream %p.\n",
		    bitrateFeedbackMode == BitrateFeedbackTMMBR ? "TMMBR+REMB" : "REMB",
		    MediaFrame::TypeToString(media), this);
	if (useTransportCC)
		Log("Activated transport-cc on %s stream %p, extmap id=%d.\n",
		    MediaFrame::TypeToString(media), this, transportCCExtId);
	mutex.unlock();
	return 1;
}

int RTPSession::SetLocalSTUNCredentials(const char* username, const char* pwd)
{
	Log("-SetLocalSTUNCredentials [frag:%s,pwd:%s]\n",username,pwd);
	//Clean mem
	if (iceLocalUsername)
		free(iceLocalUsername);
	if (iceLocalPwd)
		free(iceLocalPwd);
	//Store values
	iceLocalUsername = strdup(username);
	iceLocalPwd = strdup(pwd);
	//Ok
	return 1;
}


int RTPSession::SetRemoteSTUNCredentials(const char* username, const char* pwd)
{
	Log("-SetRemoteSTUNCredentials [frag:%s,pwd:%s]\n",username,pwd);

	//Un mot de passe DIFFÉRENT est un redémarrage ICE (RFC 8445 §9) : la paire validée
	//appartenait à la session précédente et ne prouve plus rien. On rouvre donc la
	//validation — checks sortants réarmés, et la cible d'envoi rendue au plan de
	//contrôle. Le test porte sur la VALEUR et non sur l'appel : le contrôleur repose
	//les mêmes credentials à chaque renégociation (trafic du 2026-08-21 : le même
	//`IYAI` au décroché et au re-INVITE), et effacer là recréerait exactement le trou
	//de média que iceOwnsSendAddr évite.
	//Un PREMIER mot de passe n'est pas un redémarrage : il n'y avait rien à périmer.
	const bool iceRestart = iceRemotePwd && pwd && strcmp(iceRemotePwd,pwd)!=0;

	//Clean mem
	if (iceRemoteUsername)
		free(iceRemoteUsername);
	if (iceRemotePwd)
		free(iceRemotePwd);
	//Store values
	iceRemoteUsername = strdup(username);
	iceRemotePwd = strdup(pwd);

	if (iceRestart)
	{
		Log("-SetRemoteSTUNCredentials: redemarrage ICE, la paire validee est perimee\n");
		iceConnected    = false;
		iceCheckStarted = false;
		iceOwnsSendAddr = false;
	}
	//P3 : réveille le thread Run (eventfd du Wait, jamais perdu) pour (ré)évaluer
	//l'émission de checks STUN sortants dès que la destination sera connue.
	WakeUp();
	//Ok
	return 1;
}

int RTPSession::SetRemoteCryptoDTLS(const char *setup,const char *hash,const char *fingerprint)
{
	Log("-SetRemoteCryptoDTLS [setup:%s,hash:%s,fingerpritn:%s]\n",setup,hash,fingerprint);

	//Set Suite
	if (strcasecmp(setup,"active")==0)
		dtls.SetRemoteSetup(DTLSConnection::SETUP_ACTIVE);
	else if (strcasecmp(setup,"passive")==0)
		dtls.SetRemoteSetup(DTLSConnection::SETUP_PASSIVE);
	else if (strcasecmp(setup,"actpass")==0)
		dtls.SetRemoteSetup(DTLSConnection::SETUP_ACTPASS);
	else if (strcasecmp(setup,"holdconn")==0)
		dtls.SetRemoteSetup(DTLSConnection::SETUP_HOLDCONN);
	else
		return Error("Unsupported setup mode [%s]\n", setup);

	//Set fingerprint
	if (strcasecmp(hash,"SHA-1")==0)
		dtls.SetRemoteFingerprint(DTLSConnection::SHA1,fingerprint);
	else if (strcasecmp(hash,"SHA-256")==0)
		dtls.SetRemoteFingerprint(DTLSConnection::SHA256,fingerprint);
	else
		return Error("Unsuppoted hash type [%s]. Must me SHA-1 or SHA-256.\n", hash);

	//encript & decript
	encript = true;
	decript = true;

	//Init DTLS (génère le ClientHello dans write_bio si on est en rôle client)
	int res = dtls.Init();

	//P2 : si le pair est passive, nous sommes client -> amorcer le handshake dès
	//maintenant si la destination est déjà connue (StartSending déjà appelé), sinon
	//SetRemotePort le déclenchera. Les deux ordres d'appel sont ainsi couverts.
	RequestDTLSClientHandshake();

	return res;
}

int RTPSession::SetRemoteCryptoSDES(const char* suite, const BYTE* key, const DWORD len, int keyRank)
{
	srtp_err_status_t err;
	srtp_policy_t policy;

	Log("-Set remote RTP SDES [suite:%s keyRank=%i]\n",suite,keyRank);
	
	//empty policy
	memset(&policy, 0, sizeof(srtp_policy_t));

	if (strcmp(suite,"AES_CM_128_HMAC_SHA1_80")==0)
	{
		srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtp);
		srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);
	} else if (strcmp(suite,"AES_CM_128_HMAC_SHA1_32")==0) {
		srtp_crypto_policy_set_aes_cm_128_hmac_sha1_32(&policy.rtp);
		srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);
	} else if (strcmp(suite,"AES_CM_128_NULL_AUTH")==0) {
		srtp_crypto_policy_set_aes_cm_128_null_auth(&policy.rtp);
		srtp_crypto_policy_set_aes_cm_128_null_auth(&policy.rtcp);
	} else if (strcmp(suite,"NULL_CIPHER_HMAC_SHA1_80")==0) {
		srtp_crypto_policy_set_null_cipher_hmac_sha1_80(&policy.rtp);
		srtp_crypto_policy_set_null_cipher_hmac_sha1_80(&policy.rtcp);
	} else {
		return Error("Unknown cipher suite");
	}

	//Check sizes
	if (len!=policy.rtp.cipher_key_len)
		//Error
		return Error("Key size (%d) doesn't match the selected srtp profile (required %d)\n",len,policy.rtp.cipher_key_len);

			
	//Set policy values
	policy.ssrc.type	= ssrc_any_inbound;
	policy.ssrc.value	= 0;
	policy.key		= (BYTE *) key;
	policy.next		= NULL;

	if ( keyRank == 0)
		//Create new
		err = srtp_create(&recvSRTPSession,&policy);
	else
		err = srtp_create(&recvSRTPSession_secondary,&policy);
		
	//Check error
	if (err!=srtp_err_status_ok)
		//Error
		return Error("Failed set remote SDES  (%d)\n", err);

	if ( keyRank == 0)
		//Create new
		err = srtp_create(&recvSRTPSessionRTX,&policy);
	else
		err = srtp_create(&recvSRTPSessionRTX_secondary,&policy);

	//Check error
	if (err!=srtp_err_status_ok)
		//Error
		Error("------------------------------------Failed set remote RTX SDES  (%d)\n", err);

	//Everything ok
	return 1;
}

int RTPSession::SetRemoteCryptoSDES(const char* suite, const char* key64,int keyRank)
{
	const char * sep;
	char key64copy[200];
	//Log
	Log("-SetRemoteCryptoSDES [%p] [key:%s,suite:%s]\n",this,key64,suite);

	//Decript
	decript = true;

	//Get length
	WORD len64 = strlen(key64);
	
	//Allocate memory for the key
	BYTE recvKey[len64];
	//Decode
	if ( len64 > sizeof(key64copy) )
	{
		return Error("remote SDES key too long. Len = %d.\n", len64);
	}

	sep = strchr(key64, '|');
	if (sep != NULL)
	{
	    len64 = sep - key64;
	    Log("-SetRemoteCryptoSDES found additional info. key=%.*s.\n", len64, key64);
	    strncpy( key64copy, key64, len64 );
	    key64copy[len64] = 0;    
	    Log("-SetRemoteCryptoSDES found additional info. key=%s.\n", key64copy);
	}
	else
	{
	    strcpy( key64copy, key64 );
	}

	WORD len = av_base64_decode(recvKey,key64copy,len64);
	if ( len == (WORD) -1 )
	{
	    Error("-SetRemoteCryptoSDES: fail to decode base64 DES key %s len=%d.\n", key64, len64);
	    return 0;
	}

	//Set it
	return SetRemoteCryptoSDES(suite,recvKey,len,keyRank);
}

void RTPSession::SetReceivingRTPMap(RTPMap &map)
{
	//L'ancienne map n'est pas détruite : elle DEVIENT la map de repli, le temps
	//que le pair reçoive notre réponse et bascule sur la nouvelle numérotation
	//(cf. rtpMapInPrev et CodecFromPreviousMap). Celle d'avant, elle, a fait son
	//temps. Rien de concurrent ici : l'appelant arrête la réception avant de
	//changer la map (Endpoint::StartReceiving), et c'est pour cette même course
	//qu'il le fait.
	if (rtpMapInPrev)
		delete(rtpMapInPrev);
	rtpMapInPrev = rtpMapIn;
	if (rtpMapInPrev)
		getUpdDifTime(&rtpMapInPrevSince);
	//Clone it
	rtpMapIn = new RTPMap(map);

	//Nouvelle map = nouvel épisode : ce qui a été jeté ou rattrapé sous l'ancienne
	//numérotation est clos, et le premier paquet inattendu de la nouvelle doit
	//se tracer tout de suite plutôt que d'être absorbé par le résumé en cours.
	if (unknownPtCount)
		Log("-RTP [%ls,%s,local:%d] renegotiated after dropping %u packet(s) with "
		    "payload type %d absent from the receiving map\n",
		    LabelForLog(), MediaFrame::TypeToString(media), simPort,
		    unknownPtCount, unknownPtType);
	if (salvagedCount)
		Log("-RTP [%ls,%s,local:%d] renegotiated after salvaging %u packet(s) sent "
		    "under the previous payload type %d\n",
		    LabelForLog(), MediaFrame::TypeToString(media), simPort,
		    salvagedCount, salvagedType);
	unknownPtCount = 0;
	setZeroTime(&unknownPtLast);
	salvagedCount = 0;
	setZeroTime(&salvagedLast);
}

//Le pair a-t-il basculé sur la nouvelle numérotation ? Si oui, le repli n'a plus
//de raison d'être et il est fermé SUR-LE-CHAMP : le laisser vivre les cinq
//secondes de sa borne, c'est accepter de l'ancien encore longtemps après que
//l'offre/réponse a convergé — et pendant tout ce temps, un pair qui se remettrait
//à émettre l'ancien numéro pour une autre raison (recyclage du numéro par un
//équipement intermédiaire) serait servi au lieu d'être refusé.
//
//La preuve n'est pas « un paquet valide » : c'est un payload type que SEULE la
//nouvelle map porte. Un numéro commun aux deux — PCMU 0, telephone-event 101,
//tout ce que la renégociation n'a pas touché — ne prouve rien du tout, puisque le
//pair l'émettait déjà avant. Le fermer là-dessus rouvrirait le trou exact que ce
//rattrapage vient boucher.
//
//Le désarmement seul est fait ici, pas la libération : nous sommes dans le thread
//de réception, et `rtpMapInPrev` appartient au thread de contrôle, qui le détruit
//à la renégociation suivante — réception arrêtée (Endpoint::StartReceiving).
void RTPSession::RetirePreviousMap(BYTE type)
{
	//Rien d'armé, ou numéro déjà connu de l'ancienne map : aucune preuve.
	if (!rtpMapInPrev || isZeroTime(&rtpMapInPrevSince) ||
	    rtpMapInPrev->GetCodecForType(type)!=RTPMap::NotFound)
		return;

	if (salvagedCount)
		Log("-RTP [%ls,%s,local:%d] peer switched to the new numbering (payload type "
		    "%d) after %u salvaged packet(s): the previous receiving map is retired\n",
		    LabelForLog(), MediaFrame::TypeToString(media), simPort, type, salvagedCount);

	setZeroTime(&rtpMapInPrevSince);
	salvagedCount = 0;
	setZeroTime(&salvagedLast);
}

//Le payload type sous lequel la map COURANTE porte ce codec, s'il y est encore.
BYTE RTPSession::CurrentTypeForCodec(BYTE codec) const
{
	if (!rtpMapIn)
		return RTPMap::NotFound;

	for (RTPMap::const_iterator it=rtpMapIn->begin(); it!=rtpMapIn->end(); ++it)
		if (it->second==codec)
			return it->first;

	return RTPMap::NotFound;
}

//Rattrapage de renégociation : un paquet arrive avec un payload type que la map
//courante ne porte plus, mais que la PRÉCÉDENTE portait.
//
//C'est le trou de l'offre/réponse (RFC 3264 §8) : nous appliquons la nouvelle
//numérotation dès que nous répondons, l'offreur ne bascule qu'en RECEVANT cette
//réponse, et entre les deux il émet encore sous l'ancienne. Un re-INVITE de
//Linphone qui renumérote ses payload types dynamiques ouvrait ainsi une fenêtre
//de plusieurs centaines de millisecondes où tout était jeté — inaudible en audio,
//VISIBLE en vidéo : le décodeur perd tout ce qui suit la dernière intra reçue, et
//l'image reste figée jusqu'à la suivante.
//
//La garde est le codec, pas le payload type : le paquet n'est accepté que si son
//ancien codec est TOUJOURS porté par la map courante (sous un autre numéro). Ce
//qui est réparé est alors une renumérotation, rien d'autre — le flux qui traverse
//est celui que la négociation en cours autorise, et l'aval le lit par
//`packet->SetCodec()`, pas par le numéro resté dans l'en-tête. Un codec réellement
//retiré de la négociation, lui, reste un rebut : le laisser passer ferait décoder
//au pair d'en face un format qu'il vient de refuser.
BYTE RTPSession::CodecFromPreviousMap(BYTE type)
{
	//Aucune renégociation, ou repli périmé
	if (!rtpMapInPrev || isZeroTime(&rtpMapInPrevSince) ||
	    (getDifTime(&rtpMapInPrevSince)/1000) > RTP_MAP_FALLBACK_MS)
		return RTPMap::NotFound;

	BYTE codec = rtpMapInPrev->GetCodecForType(type);
	if (codec==RTPMap::NotFound)
		return RTPMap::NotFound;

	//Le codec doit toujours être négocié, sous un numéro ou un autre
	BYTE current = CurrentTypeForCodec(codec);
	if (current==RTPMap::NotFound)
		return RTPMap::NotFound;

	//Trace agrégée : le premier paquet rattrapé de l'épisode, puis un compte
	bool first = (salvagedCount==0 || type!=salvagedType || isZeroTime(&salvagedLast));

	if (!first && (getDifTime(&salvagedLast)/1000) < UNKNOWN_PT_LOG_PERIOD_MS)
	{
		salvagedCount++;
		return codec;
	}

	if (first)
	{
		salvagedType = type;
		salvagedCount = 1;

		Log("-RTP [%ls,%s,local:%d] salvaging packets still sent with the previous "
		    "payload type %d for %s (renumbered to %d by the renegotiation): the peer "
		    "has not seen our answer yet\n",
		    LabelForLog(), MediaFrame::TypeToString(media), simPort,
		    type, GetNameForCodec(media,codec), current);
	}
	else
	{
		salvagedCount++;

		Log("-RTP [%ls,%s,local:%d] still salvaging the previous payload type %d "
		    "(%s) — %u since the first one\n",
		    LabelForLog(), MediaFrame::TypeToString(media), simPort,
		    type, GetNameForCodec(media,codec), salvagedCount);
	}

	getUpdDifTime(&salvagedLast);
	return codec;
}

//Un paquet reçu dont le payload type n'est pas dans la map de réception.
//
//Ce n'est pas une anomalie du serveur : pendant une renégociation, l'offreur
//continue d'émettre avec l'ANCIENNE numérotation jusqu'à ce qu'il reçoive la
//réponse (RFC 3264 §8) — un re-INVITE de Linphone qui déplace OPUS de 96 à 98
//produit ainsi quelques centaines de millisecondes de paquets indécodables. Le
//paquet est jeté (nous ne savons pas quel codec il porte), et la trace dit de
//QUELLE patte, de QUEL média et de QUELLE source elle parle — la question à
//laquelle l'ancienne ligne "-RTP packet type unknown [96]", répétée deux cents
//fois sans un mot de contexte, ne répondait pas.
int RTPSession::OnUnknownPayloadType(BYTE type, DWORD ssrc, const IPEndpoint& from)
{
	//Une trace par épisode, puis un résumé compté. Un changement de payload type
	//rouvre un épisode : c'est une autre cause, pas la suite de la même.
	bool first = (unknownPtCount==0 || type!=unknownPtType || isZeroTime(&unknownPtLast));

	if (!first && (getDifTime(&unknownPtLast)/1000) < UNKNOWN_PT_LOG_PERIOD_MS)
	{
		//Même épisode, trop tôt pour reparler : compter et se taire
		unknownPtCount++;
		return 0;
	}

	if (first)
	{
		unknownPtType = type;
		unknownPtCount = 1;

		Error("-RTP received a packet whose payload type is not negotiated "
		      "[pt:%d,ssrc:%x,media:%s,role:%d,label:%ls,local port:%d,from:%s:%d] "
		      "— dropped (normal while a renegotiation is in flight)\n",
		      type, ssrc, MediaFrame::TypeToString(media), role, LabelForLog(), simPort,
		      from.Address().ToString().c_str(), from.Port());
	}
	else
	{
		unknownPtCount++;

		Error("-RTP [%ls,%s,local:%d] still dropping packets with the unnegotiated "
		      "payload type %d [ssrc:%x] — %u since the first one\n",
		      LabelForLog(), MediaFrame::TypeToString(media), simPort,
		      type, ssrc, unknownPtCount);
	}

	getUpdDifTime(&unknownPtLast);
	return 0;
}

int RTPSession::SetLocalPort(int recvPort)
{
	//Override
	simPort = recvPort;
	return 0;
}

int RTPSession::GetLocalPort()
{
	// Return local
	return simPort;
}

int RTPSession::GetRemotePort()
{
	// Return local
	return sendAddr.Port();
}

bool RTPSession::CanSendCodec(DWORD codec)
{
	//Check rtp map
	if (!rtpMapOut)
		return false;

	//Même parcours que SetSendingCodec, sans bascule ni journal : la sonde
	//d'arbitrage du pont interroge chaque puits, et « absent » y est une
	//réponse nominale, pas une erreur.
	for (RTPMap::iterator it = rtpMapOut->begin(); it!=rtpMapOut->end(); ++it)
		if (it->second==codec)
			return true;

	return false;
}

bool RTPSession::SetSendingCodec(DWORD codec)
{
	//Check rtp map
	if (!rtpMapOut)
		//Error
		return Error("-SetSendingCodec error: no out RTP map\n");

	//Try to find it in the map
	for (RTPMap::iterator it = rtpMapOut->begin(); it!=rtpMapOut->end(); ++it)
	{
		//Is it ourr codec
		if (it->second==codec)
		{
			//Get type
			DWORD type = it->first;
			//Log it
			Log("-SetSendingCodec [codec:%s,type:%d]\n",GetNameForCodec(media,codec),type);
			//Set type in header
			((rtp_hdr_t *)sendPacket)->pt = type;
			//Set type
			sendType = type;
			//and we are done
			return true;
		}
	}

	//Not found
	return Error("-SetSendingCodec error: codec mapping not found [codec:%s]\n",GetNameForCodec(media,codec));
}

/***********************************
* NatCorrectable
*	Règle commune aux rattrapages RTP et RTCP : le contrôleur l'autorise, ICE
*	n'est pas en jeu, et l'adresse annoncée est privée. La *preuve* (un paquet
*	réellement reçu d'ailleurs) est vérifiée par chaque appelant.
***********************************/
bool RTPSession::NatCorrectable(const IPAddress& announced)
{
	//Le contrôleur n'a pas activé le rattrapage sur cette jambe
	if (!natLatch)
		return false;

	//ICE possède déjà la cible d'envoi (OnICEConnectivityConfirmed la pose lui-même
	//sur le pair validé) : ne pas la lui disputer. Le critère est le pair, PAS nous :
	//offrir ICE n'est pas le pratiquer. iceLocalPwd seul dit « nous avons proposé »,
	//et un pair qui répond sans ICE laisse alors la cible à personne — les checks
	//entrants sont jetés faute d'iceRemotePwd, et rien ne posera jamais l'adresse.
	if (iceRemotePwd)
		return false;

	//Plages privées v4 au sens propre (10/8, 172.16/12, 192.168/16, 100.64/10,
	//169.254/16) : exactement l'ancienne IsRFC1918, désormais portée par IPAddress.
	//IsPrivate() NE convient PAS ici : elle répond « non routable », ce qui couvre
	//aussi les plages de documentation ou réservées — nullement NATées.
	return announced.IsPrivateV4();
}

/***********************************
* SetRemotePort
*	Inicia la sesion rtp de video remota
***********************************/
int RTPSession::SetRemotePort(char *ip,int sendPort)
{
	//Une adresse illisible ne doit PAS devenir une destination : inet_addr rendait
	//INADDR_NONE aussi bien sur une erreur de format que sur l'adresse de diffusion,
	//et ce retour n'était pas testé — la destination devenait 255.255.255.255 et le
	//mediaserver émettait le flux en BROADCAST sur le LAN, sans un mot dans le log.
	//IPAddress::Parse tranche les deux familles et distingue l'erreur ; tous les
	//appelants traitent déjà 0 en erreur.
	const IPAddress remote = ip ? IPAddress::Parse(ip) : IPAddress();

	if (!remote.IsSet())
		return Error("-SetRemotePort: adresse invalide [%s]\n",ip?ip:"(null)");

	//Multicast, ou link-local sans zone : ce ne sont pas des destinations unicast.
	//Émettre vers ff02::1 arroserait tout le segment ; vers fe80::1 sans zone, le
	//noyau ne saurait par quelle interface sortir.
	if (!remote.IsUnspecified() && !remote.IsUnicastDestination())
		return Error("-SetRemotePort: adresse inutilisable comme destination [%s]\n",ip);

	//Un contrôleur qui passe 0.0.0.0 — ou :: — demande explicitement le latch : il
	//ne connaît pas la vraie adresse du pair et s'en remet à la source réellement
	//observée. Vaut autorisation, au même titre que la propriété RTP "natLatch".
	const bool latchRequest = remote.IsUnspecified();

	if (latchRequest)
		natLatch = true;

	//If we already have one and it is a NATed
	if (HasRecIP() && latchRequest)
	{
		//Exit
		Log("-SetRemotePort NAT already bound to [%s:%d]\n",sendAddr.Address().ToString().c_str(),recPort);

		return 1;
	}
	//Ok, let's et it
	Log("-SetRemotePort [%s:%d]\n",ip,sendPort);

	//Nouvelle cible posée par le plan de contrôle (re-INVITE, UPDATE…) : une
	//correction précédente porte sur l'ancienne, elle est caduque. On rouvre le droit
	//au rattrapage, sinon un pair qui change de mapping resterait coincé sur l'ancien.
	natCorrected = false;
	natRtcpCorrected = false;

	//Ip y puerto de destino. L'adresse NON SPÉCIFIÉE (0.0.0.0 ou ::) n'en est pas
	//une : c'est une demande de latch, et la destination doit rester INCONNUE
	//jusqu'à ce qu'un paquet nous apprenne d'où parle le pair. La poser
	//telle quelle ferait croire à SendPacket qu'il a une cible — et il émettrait
	//vers 0.0.0.0. C'est ce que la sentinelle INADDR_ANY disait avant, en
	//confondant « pas d'adresse » avec « l'adresse 0.0.0.0 » ; ici les deux sont
	//distincts, et c'est l'endpoint VIDE qui porte l'absence.
	if (latchRequest)
	{
		sendAddr     = IPEndpoint();
		sendRtcpAddr = IPEndpoint();
	}
	else if (iceOwnsSendAddr)
	{
		//ICE a validé une paire : l'adresse annoncée est la moins bonne des deux
		//sources et l'écraser coupe le média. Une renégociation (re-INVITE, caméra
		//qu'on allume) rappelle StartSending avec le `c=` d'origine sans que rien
		//n'ait bougé côté réseau ; le pair ne remontre sa vraie adresse qu'au check
		//STUN suivant, d'où un trou de plusieurs centaines de millisecondes à CHAQUE
		//renégociation — 0,9 s mesurée sur la jambe WebRTC du trafic du 2026-08-21,
		//audio et vidéo ensemble. Un vrai déplacement du pair passe par un
		//redémarrage ICE, qui efface iceOwnsSendAddr et rouvre cette porte.
		//sendRtcpAddr n'est pas retouché non plus. Une jambe ICE est en rtcp-mux —
		//le RTCP part sur sendAddr et la question ne se pose pas ; hors mux, elle
		//garde la cible RTCP annoncée, ce qu'elle avait déjà.
		Log("-SetRemotePort: cible ICE validee [%s:%d] conservee, annonce [%s:%d] ignoree\n",
		    sendAddr.Address().ToString().c_str(),sendAddr.Port(),ip,sendPort);
	}
	else
	{
		sendAddr = Dest(remote,sendPort);

		//Check if doing rtcp muxing
		if (muxRTCP)
			//Same than rtp
			sendRtcpAddr = Dest(remote,sendPort);
		else
			//One more than rtp
			sendRtcpAddr = Dest(remote,sendPort+1);
	}

	//Amorçage du chemin d'envoi (ouverture NAT). En chiffré (DTLS/SRTP) on garde
	//l'ouverture minimale historique : le handshake DTLS et les checks STUN prennent
	//le relais. En clair (P6), on émet une rafale de paquets RTP valides pour faire
	//latcher un pair symétrique (comedia) — le simple rtpEmpty de 8 octets ne suffit
	//pas à un stack RTP strict.
	if (encript)
	{
		//Rien à amorcer tant que la destination est inconnue (demande de latch).
		if (HasRemote())
			sendto(simSocket,rtpEmpty,sizeof(rtpEmpty),0,sendAddr,sendAddr.Len());
	}
	else
		ArmNATPriming();

	//P2 : la destination d'envoi est désormais connue -> si on est client DTLS
	//(SetRemoteCryptoDTLS déjà appelé avec setup=passive), émettre le ClientHello.
	RequestDTLSClientHandshake();

	//Y abrimos los sockets
	return 1;
}

void RTPSession::ArmRTPTimeout(DWORD timeoutMs)
{
	//Armement du watchdog d'inactivité RTP (gap 5), piloté par le contrôleur au
	//moment du SDP answer. Le chrono part de MAINTENANT : la sonnerie (avant answer)
	//n'est jamais surveillée, et « répondu mais aucun média reçu » est détecté après
	//timeoutMs sans paquet.
	if (timeoutMs > 0)
	{
		mutex.lock();
		rtpTimeout      = timeoutMs;
		rtpTimeoutArmed = true;
		rtpTimedOut     = false;
		gettimeofday(&lastRecv,NULL);   //démarre le chrono
		mutex.unlock();
		Log("-ArmRTPTimeout: watchdog armé à %u ms [%p]\n",timeoutMs,this);

		//Si le thread dort dans poll(-1) (watchdog jusqu'ici désarmé), on le réveille
		//via l'eventfd pour qu'il reprenne l'attente bornée sans attendre un paquet.
		WakeUp();
	}
	else
	{
		//timeoutMs == 0 : désarme (p.ex. mise en attente / sendonly légitime)
		mutex.lock();
		rtpTimeoutArmed = false;
		mutex.unlock();
		Log("-ArmRTPTimeout: watchdog désarmé [%p]\n",this);
	}
}

//Borne globale de sécurité du handshake DTLS client (ms) : au-delà, on bascule
//sur le chemin d'erreur transport (onRTPTimeout -> EndpointDisconnectedEvent).
//OpenSSL abandonne en général avant (HandleTimeout renvoie -1), c'est un filet.
#define DTLS_CLIENT_HANDSHAKE_TIMEOUT 30000

/************************
* SetDTLSApplicationListener / SendDTLSApplicationData
*	Greffe d'un consommateur de données applicatives DTLS — un data channel
*	WebRTC. La session reste le porteur (ICE, DTLS, socket, latch, thread) et
*	ne connaît ni SCTP ni T.140.
*************************/
void RTPSession::SetDTLSApplicationListener(ApplicationListener* listener)
{
	appListener = listener;
	//Le DTLS livre directement au consommateur : la session ne recopie rien.
	dtls.SetApplicationListener(listener);
	//La cadence du consommateur borne l'attente du poll : réveiller la boucle
	//pour qu'elle la relise sans attendre un paquet entrant.
	setZeroTime(&lastAppTick);
	WakeUp();
}

int RTPSession::SendDTLSApplicationData(const BYTE* data,DWORD size)
{
	//Le chiffrement pousse dans write_bio ; c'est FlushDTLS qui met sur le fil,
	//vers la destination latchée. Sans destination connue, rien ne part — et le
	//consommateur n'a rien à faire de cette information : le handshake n'est pas
	//terminé non plus, donc WriteApplicationData refuse déjà.
	int len = dtls.WriteApplicationData(data,size);

	if (len <= 0)
		return 0;

	FlushDTLS();

	return len;
}

void RTPSession::FlushDTLS()
{
	//Rien à émettre tant que la destination n'est pas connue (StartSending /
	//candidat ICE / latch STUN n'ont pas encore fixé sendAddr).
	if (!HasRemote())
		return;

	BYTE buffer[MTU];
	int len;
	//Vide toutes les données DTLS en attente dans write_bio (ClientHello puis
	//flights suivants) vers la cible d'envoi, indépendamment du STUN entrant.
	while ((len = dtls.Read(buffer,MTU)) > 0)
		sendto(simSocket,buffer,len,0,sendAddr,sendAddr.Len());
}

void RTPSession::RequestDTLSClientHandshake()
{
	//No-op si on n'est pas en rôle client DTLS (rôle serveur/passive, SDES, ou DTLS
	//pas encore initialisé) : aucune régression pour setup=active / navigateurs.
	if (!dtls.IsInited() || !dtls.IsClientRole())
		return;

	//Réveille le thread Run (eventfd) pour qu'il pilote le handshake sans attendre
	//un paquet entrant (le pair ICE-lite/passive n'en enverra pas).
	WakeUp();
}

void RTPSession::DriveDTLSClientHandshake()
{
	//Rôle client uniquement, DTLS prêt, handshake pas terminé, destination connue.
	if (!dtls.IsInited() || !dtls.IsClientRole() || dtls.IsHandshakeCompleted())
		return;
	if (!HasRemote())
		return;

	//Première émission : le ClientHello généré par dtls.Init() attend dans write_bio.
	if (!dtlsClientStarted)
	{
		Log("-RTPSession DTLS client: émission du ClientHello vers [%s:%d] [%p]\n",
			sendAddr.Address().ToString().c_str(),sendAddr.Port(),this);
		dtlsClientStarted = true;
		dtlsClientFailed  = false;
		gettimeofday(&dtlsClientStart,NULL);
		FlushDTLS();
		return;
	}

	//Retransmissions pilotées par OpenSSL (backoff exponentiel DTLS).
	int r = dtls.HandleTimeout();
	if (r>0)
		//Un flight a été re-mis en file d'attente : on le pousse sur le fil.
		FlushDTLS();
	else if (r<0 && !dtlsClientFailed)
	{
		Error("-RTPSession DTLS client: échec du handshake (trop de retransmissions) [%p]\n",this);
		dtlsClientFailed = true;
		if (auto l = LockListener())
			l->onRTPTimeout(this);
		return;
	}

	//Filet de sécurité : borne globale même si OpenSSL ne rend pas -1.
	if (!dtlsClientFailed && (getDifTime(&dtlsClientStart)/1000) > DTLS_CLIENT_HANDSHAKE_TIMEOUT)
	{
		Error("-RTPSession DTLS client: timeout global du handshake (%d ms) [%p]\n",
			DTLS_CLIENT_HANDSHAKE_TIMEOUT,this);
		dtlsClientFailed = true;
		if (auto l = LockListener())
			l->onRTPTimeout(this);
	}
}

//P3 : bornes de retransmission des binding requests STUN sortants (backoff)
#define ICE_CHECK_MIN_RTO 250   //ms : premier intervalle de retransmission
#define ICE_CHECK_MAX_RTO 1500  //ms : intervalle maximal (doublement borné)

void RTPSession::SendICEBindingRequest()
{
	//Il faut les credentials (locales+distantes) et une destination connue.
	if (!iceLocalUsername || !iceRemoteUsername || !iceRemotePwd)
		return;
	if (!HasRemote())
		return;

	//Transaction id (0 | timestamp), mémorisé pour corréler la réponse.
	set4(iceCheckTransId,0,0);
	set8(iceCheckTransId,4,getTime());

	//USERNAME = "remoteUfrag:localUfrag" : AddUsernameAttribute(local,remote) produit
	//bien "remote:local". MESSAGE-INTEGRITY clé = mot de passe DISTANT. Rôle
	//CONTROLLING + USE-CANDIDATE : nomination agressive, acceptable pour un pair lite.
	STUNMessage *request = new STUNMessage(STUNMessage::Request,STUNMessage::Binding,iceCheckTransId);
	request->AddUsernameAttribute(iceLocalUsername,iceRemoteUsername);
	request->AddAttribute(STUNMessage::Attribute::IceControlling,(QWORD)-1);
	request->AddAttribute(STUNMessage::Attribute::UseCandidate);
	request->AddAttribute(STUNMessage::Attribute::Priority,(DWORD)33554431);

	DWORD size = request->GetSize();
	BYTE* aux = (BYTE*)malloc(size);
	DWORD len = request->AuthenticatedFingerPrint(aux,size,iceRemotePwd);
	if (len)
	{
		Debug("ICE: envoi Binding Request sortant (user=%s) vers %s:%d [%p]\n",
			iceRemoteUsername, sendAddr.Address().ToString().c_str(), sendAddr.Port(), this);
		sendto(simSocket,aux,len,0,sendAddr,sendAddr.Len());
	}
	free(aux);
	delete(request);
}

void RTPSession::DriveICEChecks()
{
	//Actif uniquement si : creds locales+distantes connues, destination connue,
	//connectivité pas encore confirmée. Constant face à un pair ICE-lite (qui n'initie
	//jamais) ; inoffensif face à un pair full (il répond -> iceConnected -> on s'arrête).
	if (iceConnected)
		return;
	if (!iceLocalUsername || !iceRemoteUsername || !iceRemotePwd)
		return;
	if (!HasRemote())
		return;

	//Première émission immédiate.
	if (!iceCheckStarted)
	{
		iceCheckStarted = true;
		iceCheckRto     = ICE_CHECK_MIN_RTO;
		SendICEBindingRequest();
		gettimeofday(&iceLastCheck,NULL);
		return;
	}

	//Retransmission avec backoff exponentiel borné jusqu'à réception d'une réponse.
	if ((getDifTime(&iceLastCheck)/1000) >= iceCheckRto)
	{
		SendICEBindingRequest();
		gettimeofday(&iceLastCheck,NULL);
		if (iceCheckRto < ICE_CHECK_MAX_RTO)
			iceCheckRto *= 2;
	}
}

void RTPSession::OnICEConnectivityConfirmed(const IPEndpoint& from)
{
	//Latch symétrique de l'adresse distante si pas encore fixée.
	if (!HasRecIP())
	{
		recIP   = from.Address();
		recPort = from.Port();
	}
	if (!HasRemote())
	{
		sendAddr = Dest(from.Address(),from.Port());
		//Posée par ICE sur une paire validée : un SetRemotePort ultérieur ne l'écrase pas
		iceOwnsSendAddr = true;
	}

	if (!iceConnected)
	{
		Log("-RTPSession ICE: connectivité confirmée avec [%s:%d] [%p]\n",
			from.Address().ToString().c_str(), from.Port(), this);
		//Connectivité validée : on cesse d'émettre des checks sortants. Le handshake
		//DTLS client (P2) et le média ne sont pas gatés par l'ICE dans cette
		//implémentation (cf. audit P1) : ils sont pilotés indépendamment par la boucle
		//Run. Marquer iceConnected débloque donc simplement l'arrêt des checks.
		iceConnected = true;
	}
}

int RTPSession::AddICECandidate(const char* candidate)
{
	//Trickle ICE Niveau 1 pragmatique (gap 1) : on parse la ligne SDP "candidate:"
	//et, pour un candidat host/srflx dont la priorité dépasse la meilleure connue,
	//on bascule la cible d'envoi RTP/RTCP. Combiné à l'apprentissage d'adresse par
	//STUN entrant déjà présent, cela couvre le cas « un candidat gagnant arrive
	//après le SDP initial » sans agent ICE complet (Niveau 2 hors périmètre).
	if (!candidate)
		return Error("-AddICECandidate: candidat nul\n");

	//Saute le préfixe optionnel "candidate:"
	const char* p = candidate;
	if (strncmp(p,"candidate:",10)==0)
		p += 10;

	//candidate:<fnd> <cmp> <transport> <prio> <addr> <port> typ <type> ...
	char foundation[64], transport[16], address[128], typ[8], candType[32];
	unsigned int priority = 0;
	int component = 0, port = 0;
	int n = sscanf(p,"%63s %d %15s %u %127s %d %7s %31s",
		foundation,&component,transport,&priority,address,&port,typ,candType);
	if (n < 8)
		return Error("-AddICECandidate: format non reconnu [%s]\n",candidate);

	//On ne pilote la cible d'envoi que sur la composante RTP (1) en UDP
	if (component != 1 || strcasecmp(transport,"UDP")!=0)
	{
		Log("-AddICECandidate: ignoré (component=%d transport=%s)\n",component,transport);
		return 1;
	}

	//Niveau 1 : seuls les candidats host/srflx sont considérés
	if (strcasecmp(candType,"host")!=0 && strcasecmp(candType,"srflx")!=0)
	{
		Log("-AddICECandidate: type [%s] ignoré (Niveau 1)\n",candType);
		return 1;
	}

	//Ne reconfigure que si la priorité dépasse la meilleure déjà retenue
	if (iceRemotePriority != 0 && priority <= iceRemotePriority)
	{
		Log("-AddICECandidate: priorité %u <= courante %u, ignoré\n",priority,iceRemotePriority);
		return 1;
	}

	//Résout l'adresse. Les deux familles : un navigateur émet systématiquement
	//des candidats v6 quand il en a, et une link-local AVEC zone (fe80::1%eth0)
	//est la seule forme utilisable de link-local.
	const IPAddress candidateIp = IPAddress::Parse(address);
	if (!candidateIp.IsSet() || !candidateIp.IsUnicastDestination())
		return Error("-AddICECandidate: adresse invalide [%s]\n",address);

	Log("-AddICECandidate: bascule cible d'envoi vers [%s:%d] typ %s prio %u\n",address,port,candType,priority);

	//Reconfigure la cible d'envoi RTP/RTCP
	mutex.lock();
	sendAddr     = Dest(candidateIp,port);
	sendRtcpAddr = Dest(candidateIp,muxRTCP ? port : port+1);
	iceRemotePriority = priority;
	mutex.unlock();

	//Amorce le chemin (ouverture NAT ; le STUN entrant confirmera la connectivité)
	sendto(simSocket,rtpEmpty,sizeof(rtpEmpty),0,sendAddr,sendAddr.Len());

	//P2 : un candidat gagnant fournit une destination -> amorcer le ClientHello si
	//on est client DTLS et que StartSending n'a pas encore fixé l'adresse.
	RequestDTLSClientHandshake();

	return 1;
}

int RTPSession::SendEmptyPacket()
{
	//Check if we have sendinf ip address
	if (!HasRemote())
		//Exit
		return 0;

	//Open rtp and rtcp ports
	sendto(simSocket,rtpEmpty,sizeof(rtpEmpty),0,sendAddr,sendAddr.Len());

	//ok
	return 1;
}

//P6 : construit et émet UN paquet RTP valide (en-tête 12 octets, payload vide) vers
//la destination, à des fins d'amorçage NAT symétrique. Un en-tête RTPv2 complet (avec
//SSRC) suffit à faire latcher un pair comedia, contrairement au rtpEmpty tronqué.
//Décrémente le compteur de rafale. À n'utiliser qu'en clair (voir ArmNATPriming).
int RTPSession::SendNATPrimingPacket()
{
	//Destination inconnue : rien à émettre
	if (!HasRemote())
		return 0;

	//En-tête RTP minimal mais valide (V=2, P=0, X=0, CC=0, M=0). PT = type d'envoi
	//négocié si connu, sinon PCMU(0) : le latch comedia ne dépend pas du PT.
	BYTE packet[12];
	int pt = (sendType >= 0) ? sendType : 0;
	packet[0] = 0x80;
	packet[1] = (BYTE)(pt & 0x7f);
	set2(packet,2,sendSeq++);
	set4(packet,4,sendTime);
	set4(packet,8,sendSSRC);

	sendto(simSocket,packet,sizeof(packet),0,sendAddr,sendAddr.Len());

	if (natPrimingLeft > 0)
		natPrimingLeft--;

	return 1;
}

//P6 : (ré)arme une rafale d'amorçage NAT. Émet immédiatement le premier paquet pour
//ouvrir le pinhole au plus tôt ; le thread Run cadence les suivants (~20 ms). No-op
//en chiffré (le WebRTC amorce via STUN/DTLS) ou tant que la destination est inconnue.
void RTPSession::ArmNATPriming()
{
	if (encript)
		return;
	if (!HasRemote())
		return;

	natPrimingLeft = NAT_PRIMING_BURST;
	//Premier paquet tout de suite (SendNATPrimingPacket décrémente le compteur)
	SendNATPrimingPacket();
	gettimeofday(&natPrimingLast,NULL);

	//Réveille le thread Run (s'il tourne) pour qu'il cadence la suite de la rafale
	WakeUp();
}

void RTPSession::SetRemoteRateEstimator(RemoteRateEstimator* estimator)
{
	Log("-SetRemoteRateEstimator\n");

	//Store it
	remoteRateEstimator = estimator;

	//Add as listener
	remoteRateEstimator->AddListener(this);

	//Le BWE emetteur trace sous le meme nom de patte que l'estimateur RX
	if (estimator)
	{
		std::lock_guard<std::mutex> guard(senderBweMutex);
		senderBWE.SetEventSource(estimator->GetEventSource());
	}
}

/********************************
* Init
*	Inicia el control rtcp
********************************/
/***********************************
* SetDualStack
*	IPV6_V6ONLY=0 sur une socket v6 : elle entend alors les DEUX familles, les
*	pairs v4 arrivant en ::ffff:a.b.c.d. Sans effet sur une socket v4.
*	Un échec n'est pas fatal (certains noyaux imposent net.ipv6.bindv6only=1) :
*	on perd les pairs v4 sur cette jambe, mais la session reste utilisable en v6
*	— d'où une trace plutôt qu'un refus.
***********************************/
void RTPSession::SetDualStack(int fd)
{
	if (fd==FD_INVALID || socketFamily!=AF_INET6)
		return;

	int v6only = 0;
	if (setsockopt(fd,IPPROTO_IPV6,IPV6_V6ONLY,&v6only,sizeof(v6only))!=0)
		Error("-RTPSession cannot clear IPV6_V6ONLY on socket %d: %s — IPv4 peers will not be heard\n",
		      fd,strerror(errno));
}

/***********************************
* SetBindAddress
*	Adresse à lier par les sockets média — donc interface empruntée, donc profil
*	d'adressage effectif de cette jambe. À poser AVANT Init : après, les sockets
*	existent et changer d'adresse voudrait dire les recréer sous le média.
***********************************/
bool RTPSession::SetBindAddress(const IPAddress& addr)
{
	if (simSocket!=FD_INVALID || simRtcpSocket!=FD_INVALID)
		return Error("-RTPSession SetBindAddress: sockets deja ouvertes [%p]\n",this);

	//Vide : retour au défaut (écoute dual-stack sur toutes les interfaces).
	if (!addr.IsSet())
	{
		bindAddress = IPAddress();
		return true;
	}

	//Une adresse qu'on ne peut pas lier n'a rien à faire ici : multicast,
	//link-local sans zone... Le refus est explicite, la session garde le défaut.
	if (addr.IsMulticast() || addr.NeedsScope())
		return Error("-RTPSession SetBindAddress: adresse inutilisable [%s]\n",addr.ToString().c_str());

	bindAddress = addr;
	return true;
}

/***********************************
* Rebind
*	Relie les sockets media a une autre adresse : on arrete la reception, on
*	ferme, on rouvre, on redemarre. Le PORT LOCAL CHANGE au passage — c'est
*	pourquoi cela ne peut arriver qu'avant que le controleur n'ait publie son
*	SDP, c'est-a-dire au premier StartReceiving/StartSending qui porte un profil.
***********************************/
int RTPSession::Rebind(const IPAddress& addr)
{
	//Deja la bonne adresse (y compris « les deux vides ») : rien a faire.
	if (addr == bindAddress)
		return 1;

	const bool wasRunning = running;

	//Ferme les sockets et arrete le thread de reception
	if (wasRunning)
		End();

	if (!SetBindAddress(addr))
		return 0;

	//Rien a rouvrir si la session n'etait pas encore demarree : Init s'en
	//chargera, et prendra l'adresse au passage.
	if (!wasRunning)
		return 1;

	if (!Init())
		return Error("-RTPSession Rebind: impossible de relier les sockets sur [%s] [%p]\n",
		             addr.ToString().c_str(),this);

	Log("-RTPSession relie sur [%s], nouveau port local %d [%p]\n",
	    addr.IsSet() ? addr.ToString().c_str() : "toutes interfaces",simPort,this);

	return 1;
}

/***********************************
* SetAddressProfile
*	Le controleur choisit, le serveur detient (NETWORK-CONFIGURATION.md).
***********************************/
bool RTPSession::SetAddressProfile(const char* profile, std::string& error)
{
	AddressProfiles::Id id = AddressProfiles::Default();

	//Rien de demande : le profil par defaut, c'est-a-dire le comportement d'un
	//controleur qui ignore cette notion. On ne fixe RIEN — un appel muet ne doit
	//pas verrouiller la jambe et faire echouer un appel ulterieur explicite.
	if (!profile || !*profile)
		return true;

	if (!AddressProfiles::ParseId(profile,id))
	{
		error = std::string("profil d'adressage inconnu \"") + profile +
		        "\" (publicv4, publicv6, internalv4, internalv6)";
		return false;
	}

	//Deja fixe : le meme est un no-op, un autre est un echec. Recreer la socket
	//sous un media en cours changerait le port publie dans le SDP.
	if (addressProfileSet)
	{
		if (id == addressProfile)
			return true;

		error = std::string("profil deja fixe a ") + AddressProfiles::NameOf(addressProfile) +
		        " sur cette jambe, refus de basculer vers " + AddressProfiles::NameOf(id);
		return false;
	}

	if (!AddressProfiles::IsAvailable(id))
	{
		error = std::string("profil d'adressage ") + AddressProfiles::NameOf(id) +
		        " indisponible sur ce serveur";
		return false;
	}

	//L'adresse de bind peut etre VIDE (profil public herite du mode NAT sans
	//--nat, cf. AddressProfiles::AddPublic) : on reste alors sur l'ecoute
	//dual-stack, seule l'adresse annoncee change.
	const IPAddress bind = AddressProfiles::BindAddress(id);

	if (!Rebind(bind))
	{
		error = std::string("impossible de relier les sockets sur ") + bind.ToString();
		return false;
	}

	addressProfile    = id;
	addressProfileSet = true;

	Log("-RTPSession profil d'adressage [%s] bind [%s] annonce [%s] [%p]\n",
	    AddressProfiles::NameOf(id),
	    bind.IsSet() ? bind.ToString().c_str() : "toutes interfaces",
	    AddressProfiles::AnnouncedAddress(id).ToString().c_str(),this);

	return true;
}

/***********************************
* GetAnnouncedAddress
*	Ce que le controleur doit publier pour CETTE jambe.
***********************************/
IPAddress RTPSession::GetAnnouncedAddress() const
{
	const AddressProfiles::Id id = addressProfileSet ? addressProfile : AddressProfiles::Default();
	const IPAddress           addr = AddressProfiles::AnnouncedAddress(id);

	//Table non renseignee (tests unitaires, point d'entree qui aurait saute la
	//configuration) : on retombe sur l'adresse annoncee globale.
	if (addr.IsSet())
		return addr;

	return IPAddress::Parse(GetAnnouncedIp());
}

int RTPSession::Init()
{
	int retries = 0;
	simPort=0;	
	Log(">Init RTPSession %p for media %s and role %s\n", this, MediaFrame::TypeToString(media), MediaFrame::RoleToString(role));

	//Famille des sockets média. Par défaut AF_INET6 avec IPV6_V6ONLY=0 : UNE
	//socket entend les deux familles, un pair v4 arrivant en ::ffff:a.b.c.d.
	//C'est ce qui évite de doubler la plage de ports RTP et le thread de
	//réception. Une adresse de bind imposée par un profil d'adressage fixe en
	//revanche la famille — et donc restreint la session à celle-ci, ce qui est
	//précisément ce que le contrôleur a demandé (NETWORK-CONFIGURATION.md).
	socketFamily = bindAddress.IsSet() ? bindAddress.Family() : AF_INET6;

	//Adresse d'écoute : celle du profil, sinon « toutes interfaces » dans la
	//famille retenue.
	const IPAddress listenOn = bindAddress.IsSet() ? bindAddress : IPAddress::Any(socketFamily);
	
	//Get two consecutive ramdom ports
	while (retries++<100)
	{
		//If we have a rtp socket
		if (simSocket!=FD_INVALID)
		{
			// Close first socket
			close(simSocket);
			//No socket
			simSocket = FD_INVALID;
		}
		//If we have a rtcp socket
		if (simRtcpSocket!=FD_INVALID)
		{
			///Close it
			close(simRtcpSocket);
			//No socket
			simRtcpSocket = FD_INVALID;
		}

		//Create new sockets
		simSocket = socket(socketFamily,SOCK_DGRAM,0);
		//Dual-stack : sans cela une socket v6 n'entend QUE de l'IPv6, et la
		//bascule ferait perdre tous les pairs v4 d'un coup.
		SetDualStack(simSocket);
		//If not forced to any port
		if (!simPort)
		{
			//Get random
			simPort = (int) (RTPSession::GetMinPort()+(RTPSession::GetMaxPort()-RTPSession::GetMinPort())*double(rand()/double(RAND_MAX)));
			//Make even
			simPort &= 0xFFFFFFFE;
		}
		//Try to bind to port
		IPEndpoint rtpBind = listenOn.To(simPort);
		//Bind the rtcp socket
		if(bind(simSocket,rtpBind,rtpBind.Len())!=0)
		{
			Log("Failed to open RTP port %d : %s.\n", simPort, strerror(errno) );
			simPort=0;
			//Try again
			continue;
		}
		//Create new sockets
		simRtcpSocket = socket(socketFamily,SOCK_DGRAM,0);
		//Dual-stack aussi pour le RTCP : une correction partielle laisserait le
		//RTCP sourd en v6 alors que le RTP y entend.
		SetDualStack(simRtcpSocket);
		//Next port
		simRtcpPort = simPort+1;
		//Try to bind to port
		IPEndpoint rtcpBind = listenOn.To(simRtcpPort);
		//Bind the rtcp socket
		if(bind(simRtcpSocket,rtcpBind,rtcpBind.Len())!=0)
		{
			//Use random
			simPort = 0;
			//Try again
			Log("Failed to open RTCP port %d : %s.\n", simRtcpPort, strerror(errno) );
			continue;
		}
		//Set COS
		int cos = 5;
		setsockopt(simSocket,     SOL_SOCKET, SO_PRIORITY, &cos, sizeof(cos));
		setsockopt(simRtcpSocket, SOL_SOCKET, SO_PRIORITY, &cos, sizeof(cos));
		//Set TOS. En v6 la classe de trafic est portée par IPV6_TCLASS, pas
		//par IP_TOS : poser le mauvais niveau ne remonterait aucune erreur mais
		//laisserait le média non marqué, donc non prioritaire dans le réseau.
		int tos = 0x2E;
		if (socketFamily==AF_INET6)
		{
			setsockopt(simSocket,     IPPROTO_IPV6, IPV6_TCLASS, &tos, sizeof(tos));
			setsockopt(simRtcpSocket, IPPROTO_IPV6, IPV6_TCLASS, &tos, sizeof(tos));
		}
		else
		{
			setsockopt(simSocket,     IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
			setsockopt(simRtcpSocket, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
		}
		//Sockets non bloquantes : le reacteur draine sans jamais s'endormir dans un
		//recvfrom, et une fermeture par End() rend une erreur au lieu de bloquer.
		fcntl(simSocket,    F_SETFL,fcntl(simSocket,    F_GETFL,0) | O_NONBLOCK);
		fcntl(simRtcpSocket,F_SETFL,fcntl(simRtcpSocket,F_GETFL,0) | O_NONBLOCK);

		//Everything ok
		Log("-Got ports [%d,%d]\n",simPort,simRtcpPort);

		//Le reacteur bat cette session a partir d'ici : c'est ce qui amorce ICE et
		//DTLS sans attendre qu'un consommateur reclame un paquet.
		running = true;
		Group()->Add(this);
		//Done
		Log("<Init RTPSession\n");
		//Opened
		return 1;
	}
	//Error
	Error("RTPSession too many failed attemps opening sockets");
	
	//Failed
	return 0;
}

/*********************************
* End
*	Termina la todo
*********************************/
int RTPSession::End()
{
	//Check if not running
	if (!running)
		//Nothing
		return 0;
		
	//Stop just in case
	Stop();

	//Not running;
	running = false;
	//If got socket
	if (simSocket!=FD_INVALID)
	{
		//Will cause poll to return
		close(simSocket);
		//No sockets
		simSocket = FD_INVALID;
	}
	if (simRtcpSocket!=FD_INVALID)
	{
		//Will cause poll to return
		close(simRtcpSocket);
		//No sockets
		simRtcpSocket = FD_INVALID;
	}
	
	return 1;
}

int RTPSession::SendPacket(RTCPCompoundPacket &rtcp)
{
	BYTE data[MTU+SRTP_MAX_TRAILER_LEN] ZEROALIGNEDTO32;
	DWORD size = RTPPAYLOADSIZE;
	int ret = 0;

	//Check if we have sendinf ip address
	if (!HasRemoteRtcp() && !muxRTCP)
	{
		//Debug
		Debug("-Error sending rtcp packet, no remote IP yet\n");
		//Exit

		return 0;
	}
	//Serialize
	int len = rtcp.Serialize(data,size);
	//Check result
	if (!len)
		//Error
		return Error("Error serializing RTCP packet\n");

	//If encripted
	if (encript)
	{
		//Check  session
		if (!sendSRTPSession)
			return Error("-no sendSRTPSession\n");
		//Protect
		srtp_err_status_t err = srtp_protect_rtcp(sendSRTPSession,data,&len);
		//Check error
		if (err!=srtp_err_status_ok)
			//Nothing
			return Error("Error protecting RTCP packet [%d]\n",err);
	}

	//If muxin
	if (muxRTCP)
		//Send using RTP port
		ret = sendto(simSocket,data,len,0,sendAddr,sendAddr.Len());
	else
		//Send using RCTP port
		ret = sendto(simRtcpSocket,data,len,0,sendRtcpAddr,sendRtcpAddr.Len());

	//Check error
	if (ret<0)
	{
		int err = errno;

		Error("-Error sending RTP packet [%d]\n",err);
		//Return
		return 0;
	}

	//Exit
	return 1;
}

int RTPSession::SendPacket(RTPPacket &packet)
{
	return SendPacket(packet,packet.GetTimestamp());
}

int RTPSession::SendPacket(RTPPacket &packet,DWORD timestamp)
{
	//If session is not running drop the packet silently
	if (!running) return 0;
	
	//Check if we have sendinf ip address
	if (!HasRemote())
	{
		//Do we have rec ip?
		if (HasRecIP())
		{
			//Do NAT
			sendAddr = Dest(recIP,recPort);
			//La cible est posée : plus de rattrapage ultérieur (voir plus bas)
			natCorrected = true;
			//Log
			Log("-RTPSession NAT: Now sending %s to [%s:%d].\n", MediaFrame::TypeToString(media),sendAddr.Address().ToString().c_str(), recPort);
			//Check if using ice
		}
		else {
			//Exit
			Debug("-No remote address for [%s]\n",MediaFrame::TypeToString(media));
			//Exit
			return 0;
		}
	}
	else if ( HasRecIP() && !SameAddr(recIP,sendAddr.Address()) )
        {
		//Le pair a annoncé une adresse privée dans son SDP mais son RTP nous arrive
		//d'ailleurs : c'est le mapping d'un NAT symétrique, qui a réécrit l'adresse ET
		//le port. Émettre vers l'annonce ne mènerait nulle part — on ré-aiguille sur la
		//source réellement observée, seule preuve dont nous disposons.
		//One-shot : recIP est recalé sur *chaque* paquet de source différente (voir
		//ReadRTP), sans ce garde la cible battrait au gré du moindre paquet égaré.
		if (!natCorrected && NatCorrectable(sendAddr.Address()))
		{
			sendAddr = Dest(recIP,recPort);
			natCorrected = true;
			Log("-RTPSession NAT: %s now sending to [%s:%d] (annonce privee corrigee sur la source reelle).\n",
			    MediaFrame::TypeToString(media),sendAddr.Address().ToString().c_str(),recPort);
		}
		else
			Log("-RTPSession NAT: WARNING Trying to send packet from different ip address than receiving one.\n");
	}
	
	//Check if we need to send SR
	if (isZeroTime(&lastSR) || getDifTime(&lastSR)>4000000)
		//Send it
		SendSenderReport();
	
	//Modificamos las cabeceras del packete
	rtp_hdr_t *headers = (rtp_hdr_t *)sendPacket;
	
	// if the codec of the packet we want to send is not the same than defined, we changed it

	if (rtpMapOut->find(sendType) == rtpMapOut->end() || (*rtpMapOut)[sendType] != packet.GetCodec())
	{
		this->SetSendingCodec( packet.GetCodec());
	} 

	//Init send packet
	headers->version = RTP_VERSION;
	
	//if we detect a change of ssrc in packet, we change the ssrc
	if (lastSendSSRC != 0 && lastSendSSRC != packet.GetSSRC())
	{
		Debug("Changing sending SSRC - lastRecSSRC=%x, packet ssrc=%x \n",lastSendSSRC,packet.GetSSRC());
		sendSSRC = random();	
	}
	
	headers->ssrc = htonl(sendSSRC);
	lastSendSSRC = packet.GetSSRC();
	
/* 
	//Simulate SSRC change - for test purporse only
    if ( (sendSeq%50) == 0
	   || (sendSeq%50) == 1
	   || (sendSeq%50) == 4 )
	{
		headers->ssrc = random();
	}
*/
	// in case of bridging, we don't change the timestamp of the packet.
	if ( useOriTS && this->media != MediaFrame::Text)
	{ 
		sendLastTime = packet.GetTimestamp();
	}
	else
	{
		//Calculate last timestamp
		sendLastTime = sendTime + timestamp;

	}
	headers->ts = htonl( sendLastTime );

	//Incrementamos el numero de secuencia
	// in case of bridging , we don't change the seq num of the packet.
	if ( useOriSeqNum && this->media != MediaFrame::Text)
	{
		sendSeq = packet.GetSeqNum();
		headers->seq = htons( sendSeq );
	}
	else
	{
		headers->seq = htons( sendSeq );
		sendSeq++;
	}

	//Check seq wrap
	if( sendSeq == 0 )
	{
		//Inc cycles
		sendCycles++;
	}

	//La marca de fin de frame
	headers->m=packet.GetMark();

	//Calculamos el inicio
	int ini = sizeof(rtp_hdr_t);
	
	//If we have are using any sending extensions
	if (useAbsTime || useTransportCC)
	{
		//Get header
		rtp_hdr_ext_t* ext = (rtp_hdr_ext_t*)(sendPacket + ini);
		//Set extension header
		headers->x = 1;
		//Set magic cookie
		ext->ext_type = htons(0xBEDE);
		//Increase ini
		ini += sizeof(rtp_hdr_ext_t);
		DWORD extIni = ini;
		if (useAbsTime)
		{
			//Calculate absolute send time field (convert ms to 24-bit unsigned with 18 bit fractional part.
			// Encoding: Timestamp is in seconds, 24 bit 6.18 fixed point, yielding 64s wraparound and 3.8us resolution (one increment for each 477 bytes going out on a 1Gbps interface).
			DWORD abs = ((getTimeMS() << 18) / 1000) & 0x00ffffff;
			//Set header
			sendPacket[ini] = absSendTimeExtId << 4 | 0x02;
			//Set data
			set3(sendPacket,ini+1,abs);
			//Increase ini
			ini+=4;
		}
		if (useTransportCC)
		{
			//Un numero par TRANSMISSION en principe ; la retransmission RTX
			//repart pourtant avec le numero d'origine, comme l'abs-send-time
			//(le paquet stocke est deja chiffre, cf. ReSendPacket) : le
			//doublon se resout a l'appariement, premiere arrivee gagnante.
			sendPacket[ini] = transportCCExtId << 4 | 0x01;
			set2(sendPacket,ini+1,(WORD)(++transportSeqNum));
			ini+=3;
		}
		//Pad to 32 bit boundary
		while ((ini-extIni) & 0x03)
			sendPacket[ini++] = 0;
		//Set total length in 32bits words
		ext->len = htons((ini-extIni)/4);
	}

	//Comprobamos que quepan
	if (ini+packet.GetMediaLength()>MTU)
		return Error("SendPacket Overflow [size:%d,max:%d]\n",ini+packet.GetMediaLength(),MTU);

	//Copiamos los datos
        memcpy(sendPacket+ini,packet.GetMediaData(),packet.GetMediaLength());

	//Set pateckt length
	int len = packet.GetMediaLength()+ini;

	//Check if we ar encripted
	if (encript)
	{
		if (!sendSRTPSession)
		{
			Debug("-RTPSession: encryption is not yet setup.\n");
			return 0;
		}
		srtp_err_status_t err;
		
		//Encript
		err = srtp_protect(sendSRTPSession,sendPacket,&len);
		//Check error
		if (err!=srtp_err_status_ok)
		{
			//Nothing
			Error("Error protecting RTP packet for %s with recSSRC=%x  and for session=%p : [%d]\n",MediaFrame::TypeToString(media),packet.GetSSRC(),this, err);
		
			return -1;
		}
	}
	
	//Add it rtx queue
	if (useNACK)
	{
			//Create new pacekt
			RTPTimedPacket *rtx = new RTPTimedPacket(media,sendPacket,len);

			rtxUse.WaitUnusedAndLock();

			//Set cycles
			rtx->SetSeqCycles(sendCycles);
			//Add it to que
			rtxs[rtx->GetExtSeqNum()] = rtx;

			RTPOrderedPackets::iterator it = rtxs.begin();
			if ( it != rtxs.end() )
			{
					RTPTimedPacket *pkt = it->second;
					if ( rtxs.size() > 200)
					{
							rtxs.erase(it++);
							delete pkt;
					}
			}
			//Unlock
			rtxUse.Unlock();
	}

	//SIMULATING PACKET LOST 10%	
    /*
	int ret =0;
    if (this->media == MediaFrame::Video  && (sendSeq%10) == 0)
    {
		ret =0;
	}
	else
	{
		//Send packet
		ret = sendto(simSocket,sendPacket,len,0,sendAddr,sendAddr.Len());
	}
	*/	
	
	//Send packet
	int ret = sendto(simSocket,sendPacket,len,0,sendAddr,sendAddr.Len());

    if (ret <= 0)
	{
		Log("Failed to send RTP packet seqnum %d, errno=%d.\n", sendSeq, errno);
	}
        else
	{
		//Inc stats
		numSendPackets++;
		totalSendBytes += packet.GetMediaLength();
		if (useTransportCC)
		{
			std::lock_guard<std::mutex> guard(senderBweMutex);
			sentHistory.OnPacketSent((WORD)transportSeqNum, getTime(), len);
		}
		else
		{
			//Sans transport-cc, ProcessFeedback ne tourne jamais : c'est ici
			//que l'estimateur apprend ce que nous emettons, sinon l'etage de
			//perte des RR n'a rien a amorcer et la cible reste a 0. Avec
			//transport-cc, ProcessFeedback le nourrit deja : ne pas compter deux fois.
			std::lock_guard<std::mutex> guard(senderBweMutex);
			senderBWE.UpdateSentBitrate(getTime(), len);
		}
	}

	//Exit
	return (ret>0);
}
int RTPSession::ReadRTCP()
{
	BYTE buffer[MTU];
	IPEndpoint from_addr;


	//Receive from everywhere


	//Read rtcp socket
	int size = recvfrom(simRtcpSocket,buffer,MTU,MSG_DONTWAIT,from_addr.Data(),from_addr.LenPtr());

	
	//Check if it is an STUN request
	STUNMessage *stun = STUNMessage::Parse(buffer,size);
	
	//If it was
	if (stun)
	{
		STUNMessage::Type type = stun->GetType();
		STUNMessage::Method method = stun->GetMethod();
		
		//If it is a request
		if (type==STUNMessage::Request && method==STUNMessage::Binding && iceLocalPwd)
		{
			DWORD len = 0;
			//Create response
			STUNMessage* resp = stun->CreateResponse();
			//Add received xor mapped addres
			resp->AddXorAddressAttribute(from_addr.Sockaddr());
			//TODO: Check incoming request username attribute value starts with iceLocalUsername+":"
			//Create  response
			DWORD size = resp->GetSize();
			BYTE *aux = (BYTE*)malloc(size);

			//Check if we have local passworkd
			if (iceLocalPwd)
				//Serialize and autenticate
				len = resp->AuthenticatedFingerPrint(aux,size,iceLocalPwd);
			else
				//Do nto authenticate
				len = resp->NonAuthenticatedFingerPrint(aux,size);
			
			//Send it
			sendto(simRtcpSocket,aux,len,0,from_addr,from_addr.Len());

			//Clean memory
			free(aux);
			//Clean response
			delete(resp);

			//Do NAT
			sendRtcpAddr = Dest(from_addr.Address(),from_addr.Port());
			//Set port : c'est bien sin_port qu'il faut recopier. La ligne prenait
			//sin_addr.s_addr, donc le port RTCP de destination valait deux octets de
			//l'ADRESSE du pair — le RTCP partait dans le vide dès qu'un binding STUN
			//arrivait sur la socket RTCP (ICE sans rtcp-mux).

		}

		//Delete message
		delete(stun);
		//Exit
		return 1;
	}

	//Check if it is RTCP
	if (!RTCPCompoundPacket::IsRTCP(buffer,size))
		//Exit
		return 0;

	//Rattrapage NAT du RTCP, pendant de celui du RTP dans SendPacket. Le mapping du
	//port RTCP est indépendant de celui du RTP : on le prend sur *ce* paquet plutôt
	//que de le deviner à recPort+1. Sans objet en rtcp-mux, où le RTCP part sur
	//sendAddr (déjà corrigé côté RTP).
	if (HasRemoteRtcp()
	    && !SameAddr(sendRtcpAddr.Address(),from_addr.Address())
	    && !natRtcpCorrected
	    && NatCorrectable(sendRtcpAddr.Address()))
	{
		sendRtcpAddr = Dest(from_addr.Address(),from_addr.Port());
		natRtcpCorrected             = true;
		Log("-RTPSession NAT: RTCP now sending to %s:%d (annonce privee corrigee sur la source reelle).\n",
		    sendRtcpAddr.Address().ToString().c_str(),sendRtcpAddr.Port());
	}

	//Check if we have sendinf ip address
	if (!HasRemoteRtcp())
	{
		//Do NAT
		sendRtcpAddr = Dest(from_addr.Address(),from_addr.Port());
		//Set port

		//Cible RTCP posée : plus de rattrapage ultérieur
		natRtcpCorrected = true;
		//Log it
		Log("-Got first RTCP packet, sending to %s:%d with rtcp-muxing %s\n",
		    sendRtcpAddr.Address().ToString().c_str(),sendRtcpAddr.Port(),
		    muxRTCP ? "on": "off");
	}
	
	//Decript
	if (decript)
	{
		if (!recvSRTPSession)
			return Error("-No recvSRTPSession yet (RTCP)\n");
		//unprotect
		srtp_err_status_t err = srtp_unprotect_rtcp(recvSRTPSession,buffer,&size);
		//Check error
		if (err!=srtp_err_status_ok)
			return Error("Error unprotecting rtcp packet [%d]\n",err);
	}
	//RTCP mux disabled
	muxRTCP = false;
	//Parse it
	RTCPCompoundPacket* rtcp = RTCPCompoundPacket::Parse(buffer,size);
	//Check packet
	if (rtcp)
		//Handle incomming rtcp packets
		ProcessRTCPPacket(rtcp, from_addr.Address().ToString().c_str());
	
	//OK
	return 1;
}

/*********************************
* ProcessSTUN
*	Les checks de connectivité ICE, dans les deux sens. `stun` reste la
*	propriété de ReadRTP, qui l'alloue et le détruit.
*********************************/
void RTPSession::ProcessSTUN(STUNMessage* stun,const IPEndpoint& from_addr)
{
	STUNMessage::Type type = stun->GetType();
	STUNMessage::Method method = stun->GetMethod();
	
	//If it is a request
	if (type==STUNMessage::Request && method==STUNMessage::Binding)
	{
		DWORD len = 0;
		//Create response
		STUNMessage* resp = stun->CreateResponse();
		//Add received xor mapped addres
		resp->AddXorAddressAttribute(from_addr.Sockaddr());
		//TODO: Check incoming request username attribute value starts with iceLocalUsername+":"
		//Create  response
		DWORD size = resp->GetSize();
		BYTE *aux = (BYTE*)malloc(size);

		//Check if we have local passworkd
		Debug("ICE: receiving Binding Request from %s localPwd=%s\n", from_addr.Address().ToString().c_str(),
		    (iceLocalPwd != NULL) ? iceLocalPwd : "no password");

		// Le distant utilise ICE et nous a fourni son mot de passe.
		// Si nous ne disposons pas (encore) du nôtre, la réponse part sans
		// MESSAGE-INTEGRITY, et sera IGNOREE (RFC 5389 §10.1.2). 

		// Le contrôleur n'a pas appelé SetLocalSTUNCredentials()
		// Rapporter cela comme une erreur.
		if (iceRemotePwd && !iceLocalPwd && !iceMissingLocalPwdReported)
		{
			iceMissingLocalPwdReported = true;
			Error("-RTPSession ICE: incoming STUN request from [%s:%d] but missing our local password"
			      " on [%s,role:%d,port:%d]. Response without MESSAGE-INTEGRITY will be sent and"
			      " probably ignored by the remote party. The controller need to call SetLocalSTUNCredentials().\n",
			      from_addr.Address().ToString().c_str(),from_addr.Port(),
			      MediaFrame::TypeToString(media),role,simPort);
		}

		if (iceRemotePwd)
		{
			if (iceLocalPwd)
				//Serialize and autenticate
				len = resp->AuthenticatedFingerPrint(aux,size,iceLocalPwd);
			else
				//Do nto authenticate
				len = resp->NonAuthenticatedFingerPrint(aux,size);

			//Branche vide et non retour anticipé : le `return 0` d'avant fuyait
			//`aux`, `resp` et `stun`, et jetait un check par ailleurs valide.
			if (len)
				sendto(simSocket,aux,len,0,from_addr,from_addr.Len());
			else
				Debug("ICE: packet empty no need to send it\n");
		}
		else
		{
			Debug("ICE: No iceRemotePwd defined yet. Dropping request...\n");
		}

		//Clean memory
		free(aux);
		//Clean response
		delete(resp);

		if ( !HasIceRemote() )
		{
			iceRemoteIP = from_addr.Address();
		}

		//P3 : un check entrant valide prouve la connectivité (rôle serveur /
		//navigateur) -> on cesse nos éventuels checks sortants (pas de régression).
		if (!iceConnected && iceRemotePwd)
		{
			Log("-RTPSession ICE: connectivité confirmée (check entrant) [%p]\n",this);
			iceConnected = true;
		}

		//If set
		if (stun->HasAttribute(STUNMessage::Attribute::IceControlled)
			|| stun->HasAttribute(STUNMessage::Attribute::UseCandidate)
			|| SameAddr(iceRemoteIP,from_addr.Address()))
		{
			// We should check that username matches
			if (iceRemoteUsername)
			{
				// ICE is enabled
				if ( !HasRecIP() )
				{
					// set recIP if not set
					recIP = from_addr.Address();
					recPort = from_addr.Port();
				}
				
				
				if ( !SameAddr(sendAddr.Address(),recIP) 
				     || 
				     sendAddr.Port() != recPort )
				{
					// Do symetric RTP
					sendAddr = Dest(recIP,recPort);
					//Paire validée par un check entrant : c'est ICE qui tient
					//désormais la cible, pas le `c=` du SDP
					iceOwnsSendAddr = true;
				}
			}

			DWORD len = 0;
			//Create trans id
			BYTE transId[12];
			//Set first to 0
			set4(transId,0,0);
			//Set timestamp as trans id
			set8(transId,4,getTime());
			//Create binding request to send back
			STUNMessage *request = new STUNMessage(STUNMessage::Request,STUNMessage::Binding,transId);
			//Check usernames
			if (iceLocalUsername && iceRemoteUsername)
				//Add username
				request->AddUsernameAttribute(iceLocalUsername,iceRemoteUsername);
				//Add other attributes
			if ( stun->HasAttribute(STUNMessage::Attribute::IceControlled ) )
			{
				request->AddAttribute(STUNMessage::Attribute::IceControlling,(QWORD)-1);
				request->AddAttribute(STUNMessage::Attribute::UseCandidate);
			}
			else
				request->AddAttribute(STUNMessage::Attribute::IceControlled,(QWORD)-1);

			request->AddAttribute(STUNMessage::Attribute::Priority,(DWORD)33554431);
			//Create  request
			DWORD size = request->GetSize();
			BYTE* aux = (BYTE*)malloc(size);

			//Check remote pwd
			if (iceRemotePwd)
			{
				Debug("ICE: sending bind request with remote user=[%s], remote password=[%s] to %s:%d.\n",
			      (iceRemoteUsername != NULL) ? iceRemoteUsername : "no user",
			      (iceRemotePwd != NULL ) ? iceRemotePwd : "no pwd",
				 from_addr.Address().ToString().c_str(), from_addr.Port());
				if (iceRemotePwd)
				//Serialize and autenticate
					len = request->AuthenticatedFingerPrint(aux,size,iceRemotePwd);
				else
				//Do nto authenticate
					len = request->NonAuthenticatedFingerPrint(aux,size);

				//Send it — ou pas, si la sérialisation n'a rien produit. Ce cas
				//sortait de la fonction par un `return 0` qui sautait les trois
				//libérations ci-dessous : le tampon `aux`, la requête `request` et
				//le message `stun` fuyaient à chaque binding request qu'on n'arrivait
				//pas à signer. Rien à émettre n'est pas une raison de ne pas ranger.
				if (len)
					sendto(simSocket,aux,len,0,from_addr,from_addr.Len());
				else
					Debug("ICE: packet empty no need to send it\n");
			}

			//Clean memory
			free(aux);
			//Clean response
			delete(request);

			// Needed for DTLS in client mode (otherwise the DTLS "Client Hello" is not sent over the wire)
			//
			//…mais seulement s'il Y A un DTLS. Le demander sur une session qui n'en
			//a pas coûtait une ERR par binding request reçu, et un pair qui fait de
			//l'ICE en émet plusieurs par flux même quand nous n'avons annoncé ni ICE
			//ni DTLS : une paire d'appels Linphone en RTP clair produisait ainsi une
			//quarantaine de « DTLSConnection::Read() | SSL not yet ready » sur son
			//chemin nominal (trafic du 2026-08-14).
			//
			//Structuré en `if` et non en retour anticipé : la sortie de ce bloc
			//passe par le `delete(stun)` d'en dessous. Le `return 0` qui gardait le
			//cas « rien à émettre » le sautait, et fuyait le message STUN à chaque
			//fois — il devient la branche vide qu'il aurait toujours dû être.
			if (dtls.IsInited())
			{
				//Local : ReadRTP le prêtait, et c'était sa seule dépendance ici.
				BYTE buffer[MTU];
				len = dtls.Read(buffer,MTU);
				//Send back
				if (len)
					sendto(simSocket,buffer,len,0,from_addr,from_addr.Len());
				else
					Debug("DTLS: packet empty no need to send it\n");
			}
		}
	}
	//P3 : réponse à un binding request que NOUS avons émis (pair ICE-lite ou full).
	//Le handler historique n'acceptait que les Request : les Response étaient
	//ignorées, ce qui empêchait toute validation de connectivité côté offreur.
	else if (type==STUNMessage::Response && method==STUNMessage::Binding)
	{
		Log("ICE: réception Binding Response de %s:%d [%p]\n",
			from_addr.Address().ToString().c_str(), from_addr.Port(), this);
		//Validation pragmatique (parité avec le niveau ICE existant : pas de
		//vérif MESSAGE-INTEGRITY sur l'entrant) : une Binding Response provenant du
		//pair attendu confirme la connectivité.
		if (!HasIceRemote() || SameAddr(iceRemoteIP,from_addr.Address()))
			OnICEConnectivityConfirmed(from_addr);
		else
			Debug("ICE: Binding Response d'une source inattendue, ignorée [%p]\n",this);
	}
}

/*********************************
* GetTextPacket
*	Lee el siguiente paquete de video
*********************************/
int RTPSession::ReadRTP()
{
	BYTE data[MTU];
	BYTE *buffer = data;
	IPEndpoint from_addr;
	bool isRTX = false;


	//Receive from everywhere


	//Leemos del socket
	//Data()/LenPtr() : LenPtr repose la capacite avant chaque lecture, sinon une
	//source v6 arriverait tronquee sur un objet deja rempli par un pair v4.
	int size = recvfrom(simSocket,buffer,MTU,MSG_DONTWAIT,from_addr.Data(),from_addr.LenPtr());

	if (size <= 0)
	{
		Error("ReadRTP on fd %d error: errno=%d.\n", simSocket, errno);
		if (errno == ENOTCONN)
		{
			Error("-fd is not valid. Stoppong RTP session %p.\n", this);
			running = false;
			//Appelé DEPUIS le réacteur : le retrait est immédiat, sans attente.
			//Sans lui la session resterait inscrite avec un descripteur mort.
			Group()->Remove(this);
		}
		return 0;
	}

	//Check if it is an STUN request
	STUNMessage *stun = STUNMessage::Parse(buffer,size);
	
	//If it was
	if (stun)
	{
		ProcessSTUN(stun,from_addr);
		//Delete message
		delete(stun);
		//Exit
		return 1;
	}

	//Check if it is RTCP
	if (RTCPCompoundPacket::IsRTCP(buffer,size))
	{
		//Decript
		if (decript)
		{
			if (!recvSRTPSession)
				return Error("-No recvSRTPSession yet (RTCP)\n");
			//unprotect
			srtp_err_status_t err = srtp_unprotect_rtcp(recvSRTPSession,buffer,&size);
			//Check error
			if (err!=srtp_err_status_ok)
				return Error("Error unprotecting rtcp packet [%d]\n",err);
		}
		//RTCP mux enabled
		muxRTCP = true;
		//Parse it
		RTCPCompoundPacket* rtcp = RTCPCompoundPacket::Parse(buffer,size);
		//Check packet
		if (rtcp)
			//Handle incomming rtcp packets
			ProcessRTCPPacket(rtcp,from_addr.Address().ToString().c_str());
		
		//Skip
		return 1;
	}

	//Check if it a DTLS packet
	if (DTLSConnection::IsDTLS(buffer,size))
	{
		Log("-RTPSession DTLS: received packet from [%s:%d]\n", from_addr.Address().ToString().c_str(), from_addr.Port());
		//Feed it
		if (!dtls.Write(buffer,size))
		{
			Debug("-RTPSession DTLS: nothing to send back.\n");
			//Exit
			return 0;
		}
		//Read
		int len = dtls.Read(buffer,MTU);
		//Send it back
		if (!len)
		{
			Debug("-RTPSession DTLS: packet empty no need to send it\n");
			return 1;	
		}

		sendto(simSocket,buffer,len,0,from_addr,from_addr.Len());
		return 1;
	}

	//Double check it is an RTP packet
	if (!RTPPacket::IsRTP(buffer,size))
	{
		//Debug
		Debug("-Not RTP data recevied\n");
		//Exit
		return 1;
	}
	
	//If we don't have originating IP
	if (!SameAddr(recIP,from_addr.Address()))
	{
		//Bind it to first received packet ip
		recIP = from_addr.Address();
		//Get also port
		recPort = from_addr.Port();
		//Log
		Log("-RTPSession NAT: received packet from [%s:%d] for media %s.\n",
                   from_addr.Address().ToString().c_str(), from_addr.Port(),
                   MediaFrame::TypeToString(media));
		//Check if got listener
		if (auto l = LockListener())
			//Request a I frame
			l->onFPURequested(this);
	}

	//Check minimum size for rtp packet
	if (size<12)
		return Error("Invalid RTP packet read from fd %d. Packet too small of %d bytes.\n", simSocket, size);

	DWORD defaultSSRC = GetDefaultStream(true);
	
	//This should be improbed
	/*if (useNACK && defaultSSRC && defaultSSRC!=RTPPacket::GetSSRC(buffer))
	{
		Debug("-----nacked %x %x\n",defaultSSRC,RTPPacket::GetSSRC(buffer));
		//It is a retransmited packet
		isRTX = true;
	}*/
	
	//Check if it is encripted
	if (decript )
	{
		srtp_err_status_t err;
		//Check if it is a retransmited packet
	
		//Check session
		if (!recvSRTPSession)
			return Error("-No recvSRTPSession yet\n");

		if (!isRTX)
		{
			//unprotect
			if (defaultSSRC == 0 ||  defaultSSRC == RTPPacket::GetSSRC(buffer) )
				err = srtp_unprotect(recvSRTPSession,buffer,&size);
			else
			{
				if (recvSRTPSession_secondary)
					err = srtp_unprotect(recvSRTPSession_secondary,buffer,&size);
			}
		}
		else
		{
			//unprotect RTX
			if (defaultSSRC == 0 || defaultSSRC == RTPPacket::GetSSRC(buffer) )
				err = srtp_unprotect(recvSRTPSessionRTX,buffer,&size);
			else
			{
				if (recvSRTPSessionRTX_secondary)
					err = srtp_unprotect(recvSRTPSessionRTX_secondary,buffer,&size);
			}
		}
		
		//Check status
		if (err!=srtp_err_status_ok)
			//Error
			return Error("Error unprotecting rtp packet [%d] for RTPSession=%p, ssrc=%x defaultSSRC=%x\n",err,this, RTPPacket::GetSSRC(buffer),defaultSSRC);
	}

	//P5 : premier paquet RTP/SRTP reçu ET validé (déchiffrement réussi si SRTP => le
	//handshake DTLS est terminé ; sinon pas de crypto). On notifie le listener UNE
	//seule fois par cycle de réception (ré-armé par ArmRTPReceivedNotification).
	if (!rtpReceivedNotified)
	{
		rtpReceivedNotified = true;
		if (auto l = LockListener())
			l->onRTPPacketReceived(this);
	}

	//If it is a retransmission
	if (isRTX)
	{
		//Get original sequence number
		WORD osn = get2(buffer,sizeof(rtp_hdr_t));
		//Move origin
		for (int i=sizeof(rtp_hdr_t)-1;i>=0;--i)
			//Move
			buffer[i+2] = buffer[i];
		//Move init
		buffer+=2;
		//Set original seq num
		set2(buffer,2,osn);
	}

	//Get type
	BYTE type = RTPPacket::GetType(buffer);

	//Check rtp map
	if (!rtpMapIn)
		//Error
		return Error("-RTP map not set\n");
	
	//Set initial codec
	BYTE codec = rtpMapIn->GetCodecForType(type);

	//Renumérotation en cours de renégociation : la map précédente répond encore
	//pour le temps que le pair mette à recevoir notre réponse (§ CodecFromPreviousMap),
	//et pas une seconde de plus qu'il n'en faut — le premier numéro que seule la
	//nouvelle map porte prouve qu'il a basculé et referme le repli.
	if (codec!=RTPMap::NotFound)
		RetirePreviousMap(type);
	else
		codec = CodecFromPreviousMap(type);

	//Check codec
	if (codec==RTPMap::NotFound)
		//Exit : le paquet est indécodable, on le jette. La trace est agrégée —
		//c'est un régime normal pendant une renégociation (cf. unknownPtCount).
		return OnUnknownPayloadType(type, RTPPacket::GetSSRC(buffer), from_addr);

	//Create rtp packet
	RTPTimedPacket *packet = NULL;

	//Peek type
	if (codec==TextCodec::T140RED || codec==VideoCodec::RED)
	{
		//Create redundant type
		RTPRedundantPacket *red = new RTPRedundantPacket(media,buffer,size);
		//Get primary type
		BYTE t = red->GetPrimaryType();
		//Map primary data codec
		BYTE c = rtpMapIn->GetCodecForType(t);
		//Le bloc primaire porte son propre payload type : il est renuméroté par la
		//renégociation comme les autres, et le même rattrapage lui est dû.
		if (c==RTPMap::NotFound)
			c = CodecFromPreviousMap(t);
		//Check codec
		if (c==RTPMap::NotFound)
		{
			//Delete red packet
			delete(red);
			//Exit
			return Error("-RTP packet type unknown for primary type of redundant data [%d]\n",t);
		}
		//Set it
		red->SetPrimaryCodec(c);
		//For each redundant packet
		for (int i=0; i<red->GetRedundantCount();++i)
		{
			//Get redundant type
			BYTE t = red->GetRedundantType(i);
			//Map redundant data codec
			BYTE c = rtpMapIn->GetCodecForType(t);
			//Idem pour chaque bloc redondant (RFC 4103) : ils portent le même
			//payload type que le primaire d'un paquet antérieur.
			if (c==RTPMap::NotFound)
				c = CodecFromPreviousMap(t);
			//Check codec
			if (c==RTPMap::NotFound)
			{
				//Delete red packet
				delete(red);
				//Exit
				return Error("-RTP packet type unknown for primary type of secundary data [%d,%d]\n",i,t);
			}
			//Set it
			red->SetRedundantCodec(i,c);
		}
		//Set packet
		packet = red;
	} else {
		//Create normal packet
		packet = new RTPTimedPacket(media,buffer,size);
	}

	//Un en-tete RTP decrit sa propre longueur (CSRC, extension) : si ce qu'il
	//annonce ne tient pas dans le datagramme recu, tout ce qui suivrait lirait
	//hors du tampon du paquet. On le jette ici, une fois, plutot que de le
	//laisser traverser la chaine media.
	if (!packet->IsValid())
	{
		delete(packet);
		return Error("-RTP packet header does not fit in the received datagram [size:%d]\n",size);
	}
		//Set codec
	packet->SetCodec(codec);
	//Get ssrc
	DWORD ssrc = packet->GetSSRC();

	//Lot 4 : le pair attend nos rapports d'arrivee sur SES paquets. L'extension
	//n'est lue que si elle est negociee — sans cela, aucun appelant ne consomme
	//les extensions entrantes et le paquet n'a pas a etre relu pour rien.
	if (useTransportCC)
	{
		packet->ProcessExtensions(extMap);
		if (packet->HasTransportSeqNum())
			transportFeedback.OnPacketReceived(ssrc,packet->GetTransportSeqNum(),getTime());
	}

        streamUse.IncUse();

        //if ( defaultSSRC == 0 && defaultStream != NULL) defaultStream->Cancel();

        RTPStream* stream = getStream(ssrc);

        if (!isRTX)
        {
	    if ( stream == NULL )
	    {
                //Send SR to old one
                SendSenderReport();
                streamUse.DecUse();
				if ( defaultStream == NULL && ssrc > 0)
				{
					Log("-Creating default stream SSRC [new:%x] for RTPSession=%p \n",ssrc,this);
					SetDefaultStream(true, ssrc);
				}
				else if (auto l = LockListener()) //call listener
				{
					l->onNewStream(this, ssrc, true);
				}
				else
				{
					Log("-No Listener. Adding new SSRC [new:%x]\n",ssrc);
					if (defaultStream == NULL) 
						SetDefaultStream(true, ssrc);
					else
						ChangeStream( defaultSSRC, ssrc );
				}

                streamUse.IncUse();
                stream = getStream(ssrc);
            }

            if ( stream != NULL )
            {
                stream->Add(packet, size);
            }
        }
        else /* RTP retransmission enabled. We do not handle multi stream in that case */
        {
            if (defaultSSRC > 0)
            {
                //Set SSRC of the original stream for retransmitted packets
                if ( ssrc != defaultSSRC ) packet->SetSSRC(defaultSSRC);
            }
            else
            {
                streamUse.DecUse();
                SetDefaultStream(true, ssrc);
                streamUse.IncUse();
            }
            defaultStream->Add(packet,size);
        }
        streamUse.DecUse();
	//OK
	return 1;
}

RtpSessionSet* RTPSession::Group()
{
	return pollGroup ? pollGroup : &RtpSessionSet::Default();
}

bool RTPSession::SetPollGroup(RtpSessionSet* group)
{
	//Apres Init, la session est deja inscrite : changer de groupe ici ferait
	//retirer End() d'un groupe ou personne ne l'a jamais mise.
	if (running)
		return Error("-RTPSession: groupe de poll pose apres Init, refuse [%p]\n",this);

	pollGroup = group;
	return true;
}

void RTPSession::WakeUp()
{
	Group()->Wake();
}

void RTPSession::Stop()
{
	//Check running
	if (running)
	{
		//Not running
		running = false;

		//Retrait SYNCHRONE : au retour, le reacteur ne poll plus nos sockets et
		//n'appellera plus aucun de nos callbacks. C'est ce qui rend sur le close()
		//que End() fait juste apres — sinon le descripteur pourrait etre reattribue
		//sous les pieds du poll d'un autre thread.
		Group()->Remove(this);
                DeleteStreams();
	}

        rtxUse.WaitUnusedAndLock();
        for (RTPOrderedPackets::iterator it = rtxs.begin(); it!=rtxs.end();++it)
	{
		//Get pacekt
		RTPTimedPacket *pkt = it->second;
		//Delete object
		delete(pkt);
	}
        rtxUse.Unlock();
}

int RTPSession::Sooner(int waitMs,int candidateMs)
{
	if (candidateMs < 0)
		return waitMs;
	if (waitMs < 0)
		return candidateMs;
	return waitMs < candidateMs ? waitMs : candidateMs;
}

//P2 : rôle client, DTLS prêt, handshake non terminé, destination connue. Le cas
//serveur/passive reste inchangé.
bool RTPSession::IsDrivingDTLSClient() const
{
	return dtls.IsInited() && dtls.IsClientRole()
		&& !dtls.IsHandshakeCompleted()
		&& HasRemote();
}

//P3 : creds locales+distantes connues, destination connue, connectivité pas
//encore confirmée. Face à un pair ICE-lite qui n'initie jamais ; s'arrête dès la
//1re réponse ou le 1er check entrant.
bool RTPSession::IsDrivingICEChecks() const
{
	return !iceConnected && iceLocalUsername && iceRemoteUsername
		&& iceRemotePwd && HasRemote();
}

//P6 : rafale d'amorçage NAT armée, en clair, destination connue.
bool RTPSession::IsNATPriming() const
{
	return natPrimingLeft > 0 && !encript
		&& HasRemote();
}

int RTPSession::GetPollFds(pollfd* fds,int max)
{
	if (max < 2 || simSocket==FD_INVALID || simRtcpSocket==FD_INVALID)
		return 0;

	fds[0].fd	= simSocket;
	fds[0].events	= POLLIN | POLLERR | POLLHUP;
	fds[1].fd	= simRtcpSocket;
	fds[1].events	= POLLIN | POLLERR | POLLHUP;

	return 2;
}

int RTPSession::GetNextTimeoutMs(QWORD nowUs)
{
	//Attente infinie par défaut : une session qui n'a rien à faire ne fait pas
	//tourner le thread de son groupe. Watchdog, retransmissions du handshake
	//DTLS client, checks ICE, amorçage NAT, rapports transport-cc en attente et
	//cadence applicative sont les seules raisons de borner — la plus proche gagne.
	int waitMs = rtpTimeoutArmed ? PollTimeoutMs : -1;

	if (IsDrivingDTLSClient() || IsDrivingICEChecks())
		waitMs = Sooner(waitMs,DtlsPollTimeoutMs);

	if (IsNATPriming())
		waitMs = Sooner(waitMs,NAT_PRIMING_INTERVAL_MS);

	//Sans cette borne, le dernier rapport d'une rafale attendrait le paquet
	//entrant suivant — et il n'y en a pas toujours un (fin de parole, freeze).
	if (useTransportCC && transportFeedback.HasPending())
		waitMs = Sooner(waitMs,TransportFeedbackPollMs);

	if (ApplicationListener* app = appListener.load())
	{
		DWORD tick = app->GetApplicationTickMs();
		//0 = pas de cadence. Le passer à Sooner demanderait une attente NULLE.
		if (tick)
			waitMs = Sooner(waitMs,(int) tick);
	}

	return waitMs;
}

void RTPSession::OnPollEvents(const pollfd* fds,int count,QWORD nowUs)
{
	if (count < 2)
		return;

	if (fds[0].revents & POLLIN)
	{
		//Any inbound traffic (RTP/STUN/DTLS) prouve que le pair est vivant :
		//on mémorise l'instant et on réarme l'anti-rebond.
		gettimeofday(&lastRecv,NULL);
		rtpTimedOut = false;
		//Read rtp data
		ReadRTP();
	}

	if (fds[1].revents & POLLIN)
		//Read rtcp data
		ReadRTCP();
}

void RTPSession::OnPeriodic(QWORD nowUs)
{
	//Rapporter au pair ce qui nous est arrivé, à la cadence du générateur. APRÈS
	//les lectures : le rapport porte alors les paquets de ce tour.
	if (useTransportCC)
		SendTransportWideFeedback(nowUs);

	//Cadence la rafale d'amorçage NAT (~20 ms) tant qu'il en reste. APRÈS les
	//lectures : si le pair a déjà latché et répondu, la rafale continue quand
	//même jusqu'au bout — inoffensif, elle est courte et bornée.
	if (IsNATPriming() && (getDifTime(&natPrimingLast)/1000) >= NAT_PRIMING_INTERVAL_MS)
	{
		SendNATPrimingPacket();
		gettimeofday(&natPrimingLast,NULL);
	}

	//APRÈS les lectures : un check ou une réponse entrante a pu poser
	//iceConnected et court-circuiter l'émission.
	if (IsDrivingICEChecks())
		DriveICEChecks();

	//APRÈS les lectures aussi : les flights entrants (branche DTLS de ReadRTP)
	//font d'abord progresser le handshake.
	if (IsDrivingDTLSClient())
		DriveDTLSClientHandshake();

	//Le consommateur de données applicatives (data channel) peut avoir ses
	//propres timers et pas de thread : c'est ce tour qui les bat. UNE seule
	//lecture du pointeur : il peut être retiré entre deux, et lire « il y a une
	//cadence » puis appeler NULL est un crash.
	ApplicationListener* app = appListener.load();
	DWORD appTickMs = app ? app->GetApplicationTickMs() : 0;

	if (appTickMs)
	{
		//Écoulement RÉEL : le poll a pu rendre la main plus tôt (paquet entrant)
		//ou plus tard (charge).
		DWORD elapsed = isZeroTime(&lastAppTick) ? appTickMs
					: (DWORD)(getDifTime(&lastAppTick)/1000);

		if (elapsed >= appTickMs)
		{
			gettimeofday(&lastAppTick,NULL);
			app->onApplicationTick(elapsed);
		}
	}

	//Watchdog d'inactivité (gap 5) : armé et aucun paquet depuis > rtpTimeout
	//(mesuré depuis l'armement ou le dernier paquet) => émettre UNE seule fois
	//onRTPTimeout (anti-rebond via rtpTimedOut).
	if (rtpTimeoutArmed && rtpTimeout>0 && !rtpTimedOut
			&& (getDifTime(&lastRecv)/1000) > rtpTimeout)
	{
		//Marque la transition actif -> inactif
		rtpTimedOut = true;
		Log("-RTPSession inactivité > %u ms, notification onRTPTimeout [%p]\n",rtpTimeout,this);
		//Notifie le listener (RTPEndpoint publiera EndpointDisconnectedEvent)
		if (auto l = LockListener())
			l->onRTPTimeout(this);
	}
}

void RTPSession::OnPollError(short revents)
{
	//POLLHUP (pair parti), POLLERR (erreur socket) ou POLLNVAL (fd fermé). Le
	//réacteur nous retire après cet appel : cette session cesse de recevoir, les
	//autres jambes du groupe continuent.
	Log("-RTPSession sortie du groupe sur evenement socket 0x%x [%p]\n",revents,this);
}

void RTPSession::OnStreamsChanged()
{
	++streamGeneration;
	streamWait.Signal();
}

RTPPacket* RTPSession::GetPacket(DWORD ssrc, DWORD timeoutMs)
{
	//Lu AVANT la recherche : si un flux naît entre les deux, le prédicat de
	//l'attente le voit déjà et ne dort pas.
	const DWORD seen = streamGeneration.load();

	streamUse.IncUse();
	RTPStream* s = (ssrc != 0) ? getStream(ssrc) : defaultStream;
	const bool disabled = (s != NULL) && s->disabled;
	RTPPacket* rtp = (s != NULL && !disabled) ? s->Wait(timeoutMs) : NULL;
	streamUse.DecUse();

	if (rtp != NULL)
		return rtp;

	//Flux en cours d'arrêt (CancelStreams) : le consommateur doit sortir de sa
	//boucle tout de suite, pas attendre une échéance.
	if (disabled)
		return NULL;

	//Pas encore de flux pour ce SSRC : c'est le premier paquet reçu qui le crée.
	//On attend cette naissance. Revenir sonder faisait tourner le consommateur
	//à ~2 250 tours par seconde le temps d'un établissement d'appel.
	if (s == NULL)
	{
		streamWait.WaitUntil(timeoutMs,
			[this,seen] { return streamGeneration.load() != seen; });
	}

	return NULL;
}

void RTPSession::CancelGetPacket()
{
	//cancel
    streamUse.IncUse();
	if (defaultStream != NULL) defaultStream->Cancel();
    streamUse.DecUse();

    //HORS du verrou streamUse. Annuler `defaultStream` ne reveille que le
    //consommateur d'un flux DEJA NE. Celui d'une jambe qui n'a jamais rien recu
    //attend la NAISSANCE d'un flux, et defaultStream est NULL : personne n'etait
    //reveille, donc chaque StopReceiving coutait la borne entiere
    //(ConsumerPollMs) — 201 ms mesures en recette le 2026-09-02.
    OnStreamsChanged();
}

void RTPSession::CancelGetPacket(DWORD & ssrc)
{
    streamUse.IncUse();
    RTPStream * s = (ssrc != 0) ? getStream(ssrc) : defaultStream;
    if (s) s->Cancel();
    streamUse.DecUse();

    //Meme raison que la surcharge sans ssrc : le flux demande peut ne pas
    //exister encore, et son consommateur attend alors sa naissance.
    OnStreamsChanged();
}

void RTPSession::ResetPacket(DWORD & ssrc, bool clear) 
{ 
    streamUse.IncUse();
    RTPStream * s = (ssrc != 0) ? getStream(ssrc) : defaultStream;
    if (s) s->Reset(clear);
    streamUse.DecUse();
}
void RTPSession::ProcessRTCPPacket(RTCPCompoundPacket *rtcp, const char * fromAddr)
{
	//Verrou lecteur : DeleteStreams peut courir en parallele (voir
	//RTPSession::CreateSenderReport).
	ScopedUse scopedStreams(streamUse);
	//For each packet
	for (int i = 0; i<rtcp->GetPacketCount();i++)
	{
		//Get pacekt
		RTCPPacket* packet = rtcp->GetPacket(i);
		//Check packet type
		switch (packet->GetType())
		{
			case RTCPPacket::SenderReport:
			{
				RTCPSenderReport* sr = (RTCPSenderReport*)packet;
				//Get Timestamp, the middle 32 bits out of 64 in the NTP timestamp (as explained in Section 4) received as part of the most recent RTCP sender report (SR) packet from source SSRC_n. If no SR has been received yet, the field is set to zero.
				recSR = sr->GetTimestamp() >> 16;
				//Uptade last received SR
				getUpdDifTime(&lastReceivedSR);
				//Check recievd report
				for (int j=0;j<sr->GetCount();j++)
				{
					//Get report
					RTCPReport *report = sr->GetReport(j);
					//Check ssrc
					if (report->GetSSRC()==sendSSRC)
					{
						//Calculate RTT
						if (!isZeroTime(&lastSR) && report->GetLastSR() == sendSR)
						{
							//Calculate new rtt
							DWORD rtt = getDifTime(&lastSR)/1000-report->GetDelaySinceLastSRMilis();
							//Set it
							SetRTT(rtt);
						}
						//Le pair rapporte les pertes de NOTRE flux : elles
						//etaient decodees puis jetees (lot 6.3)
						OnReportedLoss(report->GetFactionLost());
					}
				}
				break;
			}
			case RTCPPacket::ReceiverReport:
			{
				RTCPReceiverReport* rr = (RTCPReceiverReport*)packet;
				//Check recievd report
				for (int j=0;j<rr->GetCount();j++)
				{
					//Get report
					RTCPReport *report = rr->GetReport(j);
					//Check ssrc
					if (report->GetSSRC()==sendSSRC)
					{
						//Calculate RTT
						if (!isZeroTime(&lastSR) && report->GetLastSR() == sendSR)
						{
							//Calculate new rtt
							DWORD rtt = getDifTime(&lastSR)/1000-report->GetDelaySinceLastSRMilis();
							//Set it
							SetRTT(rtt);
						}
						//Le pair rapporte les pertes de NOTRE flux : elles
						//etaient decodees puis jetees (lot 6.3)
						OnReportedLoss(report->GetFactionLost());
					}
				}
				break;
			}
				break;
			case RTCPPacket::SDES:
				break;
			case RTCPPacket::Bye:
				break;
			case RTCPPacket::App:
				break;
			case RTCPPacket::RTPFeedback:
			{
				//Get feedback packet
				RTCPRTPFeedback *fb = (RTCPRTPFeedback*) packet;
				//Check feedback type
				switch(fb->GetFeedbackType())
				{
					case RTCPRTPFeedback::NACK:
						//Debug("-Nack received\n");
						for (BYTE i=0;i<fb->GetFieldCount();i++)
						{
							//Get field
							RTCPRTPFeedback::NACKField *field = (RTCPRTPFeedback::NACKField*) fb->GetField(i);
							//Resent it
							ReSendPacket(field->pid);
							//Check each bit of the mask
							for (int i=0;i<16;i++)
								//Check it bit is present to rtx the packets
								if ((field->blp >> (15-i)) & 1)
									//Resent it
									ReSendPacket(field->pid+i+1);
						}
						break;
					case RTCPRTPFeedback::TempMaxMediaStreamBitrateRequest:
						for (BYTE i=0;i<fb->GetFieldCount();i++)
						{
							//Get field
							RTCPRTPFeedback::TempMaxMediaStreamBitrateField *field = (RTCPRTPFeedback::TempMaxMediaStreamBitrateField*) fb->GetField(i);
							//Un champ qui ne vise pas notre SSRC sortant est ignoré. Le dire :
							//sans cette trace, un pair qui se trompe de cible est indistinguable
							//d'un pair qui ne demande rien (séance du 2026-09-01 : 1365 TMMBR
							//reçus, aucune valeur lisible dans le journal).
							if (field->GetSSRC()!=sendSSRC)
							{
								Debug("-TempMaxMediaStreamBitrateRequest ignore, champ pour ssrc %u (le notre est %u) [%p]\n",
									field->GetSSRC(),sendSSRC,this);
								continue;
							}

							//La VALEUR demandée est ce qui pilote notre émission vers ce
							//pair : c'est elle qu'il faut lire, pas le seul fait d'avoir
							//reçu un TMMBR.
							Log("-TempMaxMediaStreamBitrateRequest received from [%s] on %s stream: maxBitrate = %u, overhead = %u\n",
								fromAddr,MediaFrame::TypeToString(media),
								field->GetBitrate(),field->GetOverhead());
							//call listener
							if (auto l = LockListener())
								l->onTempMaxMediaStreamBitrateRequest(this,field->GetBitrate(),field->GetOverhead());
							//RFC 5104 §4.2.1.2 : l'émetteur de média répond TMMBN, sinon
							//le pair retransmet son TMMBR à chaque intervalle RTCP —
							//exactement ce que NOUS faisons en face tant que le TMMBN
							//n'arrive pas (pendingTMBR, SendSenderReport). Répondu même
							//sans listener : la restriction est acquise au niveau session.
							SendTempMaxMediaStreamBitrateNotification(field->GetBitrate(),field->GetOverhead());
						}
						break;
					case RTCPRTPFeedback::TempMaxMediaStreamBitrateNotification:
						Debug("-TempMaxMediaStreamBitrateNotification received from [%s] on %s stream\n", fromAddr, MediaFrame::TypeToString(media));
						pendingTMBR = false;
						if (requestFPU && defaultStream != NULL)
						{
							requestFPU = false;
							DWORD ssrc=defaultStream->GetRecSSRC();
							SendFIR(ssrc);
						}
						for (BYTE i=0;i<fb->GetFieldCount();i++)
						{
							//Get field
							RTCPRTPFeedback::TempMaxMediaStreamBitrateField *field = (RTCPRTPFeedback::TempMaxMediaStreamBitrateField*) fb->GetField(i);
							Debug("-TempMaxMediaStreamBitrateNotification: maxBitrate = %d, overhead=%d\n", field->GetBitrate(), field->GetOverhead() );


						}
		
						break;
					case RTCPRTPFeedback::TransportWideFeedbackMessage:
						//Rapport du pair sur NOS paquets sortants (lot 6.3)
						ProcessTransportWideFeedback(fb);
						break;
				}
				break;
			}
			case RTCPPacket::PayloadFeedback:
			{
				//Get feedback packet
				RTCPPayloadFeedback *fb = (RTCPPayloadFeedback*) packet;
				//Check feedback type
				switch(fb->GetFeedbackType())
				{
					case RTCPPayloadFeedback::PictureLossIndication:
					case RTCPPayloadFeedback::FullIntraRequest:
						//Chec listener
						if (auto l = LockListener())
							//Send intra refresh
							l->onFPURequested(this);
						break;
					case RTCPPayloadFeedback::SliceLossIndication:
						//Le pair a perdu des tranches : sa référence est abîmée et il ne
						//se répare pas tout seul. Nous ne savons pas retransmettre une
						//tranche, donc la seule réparation dont nous disposons est une
						//intra — la même que pour un PLI, et l'aval borne déjà la cadence
						//(VideoTranscoder::RequestSourceFPU, une par seconde) et sait à
						//qui la demander selon le mode (absorbée par l'encodeur en
						//transcodage, relayée à la source en pont).
						//
						//Ceci n'était que journalisé. En pont il n'y a plus d'encodeur
						//pour produire une intra périodique : la seule source d'image clé
						//est la patte amont, et elle n'est sollicitée que si nous relayons
						//la demande. Trafic du 2026-08-21 : une rafale de pertes de 300 ms
						//au décroché abîme la référence VP8, Linphone réclame en SLI
						//pendant onze secondes, personne ne répond, et l'image reste
						//pleine d'artéfacts jusqu'au raccroché.
						Log("-RTCP SliceLossIndication received\n");
						if (auto l = LockListener())
							l->onFPURequested(this);
						break;
					case RTCPPayloadFeedback::ReferencePictureSelectionIndication:
						//PAS une demande de réparation, et surtout pas une intra : le RPSI
						//désigne une référence que le pair POSSÈDE (RFC 4585 §6.3.3), et
						//mediastreamer l'émet périodiquement sur chaque image de référence
						//correctement décodée. Dans la trace du 2026-08-21 il revient
						//toutes les secondes pendant que le décodeur tourne à 31 fps. Le
						//traiter comme un PLI arroserait la source de FIR pour toute la
						//durée de l'appel.
						Log("-RTCP ReferencePictureSelectionIndication  received\n");
						break;
					case RTCPPayloadFeedback::TemporalSpatialTradeOffRequest:
						Log("-RTCP TemporalSpatialTradeOffRequest  received\n");
						break;
					case RTCPPayloadFeedback::TemporalSpatialTradeOffNotification:
						Log("-RTCP TemporalSpatialTradeOffNotification\n");
						break;
					case RTCPPayloadFeedback::VideoBackChannelMessage:
						Log("-RTCP VideoBackChannelMessage\n");
						break;
					case RTCPPayloadFeedback::ApplicationLayerFeeedbackMessage:
						for (BYTE i=0;i<fb->GetFieldCount();i++)
						{
							//Get feedback
							RTCPPayloadFeedback::ApplicationLayerFeeedbackField* msg = (RTCPPayloadFeedback::ApplicationLayerFeeedbackField*)fb->GetField(i);
							//Get size and payload
							DWORD len	= msg->GetLength();
							BYTE* payload	= msg->GetPayload();
							//Check if it is a REMB
							if (len>8 && payload[0]=='R' && payload[1]=='E' && payload[2]=='M' && payload[3]=='B')
							{
								//GEt exponent
								BYTE exp = payload[5] >> 2;
								DWORD mantisa = payload[5] & 0x03;
								mantisa = mantisa << 8 | payload[6];
								mantisa = mantisa << 8 | payload[7];
								//Get bitrate
								DWORD bitrate = mantisa << exp;
								//Check if it is for us
								if (auto l = LockListener())
									//call listener
									l->onReceiverEstimatedMaxBitrate(this,bitrate);
							}
						}
						break;
				}
				break;
			}
			case RTCPPacket::FullIntraRequest:
				//This is message deprecated and just for H261, but just in case
				//Check listener
				if (auto l = LockListener())
					//Send intra refresh
					l->onFPURequested(this);
				break;
			case RTCPPacket::NACK:
				break;
		}
	}
	//Delete pacekt
	delete(rtcp);
}

int RTPSession::SendFIR(DWORD & ssrc)
{
	//Verrou lecteur : DeleteStreams peut courir en parallele (voir
	//RTPSession::CreateSenderReport).
	ScopedUse scopedStreams(streamUse);

	Log("-SendFIR\n");

	//Create rtcp sender retpor
	RTCPCompoundPacket* rtcp = CreateSenderReport();
	
	DWORD recSSRC = ssrc;
	if (getStream(ssrc) == NULL && recSSRC == 0 && defaultStream != NULL)
		recSSRC = defaultStream->GetRecSSRC();
		
	//Create fir request
	RTCPPayloadFeedback *fir = RTCPPayloadFeedback::Create(RTCPPayloadFeedback::FullIntraRequest,sendSSRC,recSSRC);
	//ADD field
	fir->AddField(new RTCPPayloadFeedback::FullIntraRequestField(recSSRC,firReqNum++));
	//Add to rtcp
	rtcp->AddRTCPacket(fir);

	//Add PLI
	RTCPPayloadFeedback *pli = RTCPPayloadFeedback::Create(RTCPPayloadFeedback::PictureLossIndication,sendSSRC,recSSRC);
	//Add to rtcp
	rtcp->AddRTCPacket(pli);

	//Send packet
	int ret = SendPacket(*rtcp);

	//Delete it
	delete(rtcp);

	return ret;
}

int RTPSession::SendReferencePictureSelectionIndication(DWORD ssrc, WORD pictureId)
{
	//Verrou lecteur : DeleteStreams peut courir en parallele (voir
	//RTPSession::CreateSenderReport).
	ScopedUse scopedStreams(streamUse);

	RTPStream* stream = getStream(ssrc);
	if (stream == NULL)
		stream = defaultStream;
	if (stream == NULL)
		return 0;
	DWORD recSSRC = stream->GetRecSSRC();

	//Le FCI porte le payload type du flux acquitté : le PT que le pair a
	//déclaré pour le codec reçu (rtpMapIn : clé = PT, valeur = codec)
	if (!rtpMapIn)
		return 0;
	DWORD codec = stream->GetRecCodec();
	BYTE pt = RTPMap::NotFound;
	for (RTPMap::const_iterator it = rtpMapIn->begin(); it != rtpMapIn->end(); ++it)
		if (it->second == codec)
		{
			pt = it->first;
			break;
		}
	if (pt == RTPMap::NotFound)
		return 0;

	//Bit string = le PictureID tel que reçu (RFC 7741 §5.1) : 2 octets
	//réseau si étendu (bit M, 0x8000), 1 octet sinon — l'émetteur le
	//compare à l'identique, sans le décoder
	BYTE bitString[2];
	DWORD len;
	if (pictureId & 0x8000)
	{
		bitString[0] = pictureId >> 8;
		bitString[1] = pictureId;
		len = 2;
	}
	else
	{
		bitString[0] = pictureId;
		len = 1;
	}

	Debug("-SendReferencePictureSelectionIndication pictureId=0x%.4x pt=%d ssrc=%x\n",pictureId,pt,recSSRC);

	//Create rtcp sender report
	RTCPCompoundPacket* rtcp = CreateSenderReport();

	RTCPPayloadFeedback *rpsi = RTCPPayloadFeedback::Create(RTCPPayloadFeedback::ReferencePictureSelectionIndication,sendSSRC,recSSRC);
	rpsi->AddField(new RTCPPayloadFeedback::ReferencePictureSelectionField(pt,bitString,len));
	rtcp->AddRTCPacket(rpsi);

	//Send packet
	int ret = SendPacket(*rtcp);

	//Delete it
	delete(rtcp);

	return ret;
}

int RTPSession::RequestFPU()
{
	//Verrou lecteur : DeleteStreams peut courir en parallele (voir
	//RTPSession::CreateSenderReport).
	ScopedUse scopedStreams(streamUse);
	 if (defaultStream == NULL)
		return 0;

	DWORD ssrc=defaultStream->GetRecSSRC();
	return RequestFPU(ssrc);
}

int RTPSession::RequestFPU(DWORD & ssrc)
{
	//Verrou lecteur : DeleteStreams peut courir en parallele (voir
	//RTPSession::CreateSenderReport).
	ScopedUse scopedStreams(streamUse);
	//Send all the packets inmediatelly to the decoderso I frame can be handled as soon as possoble
	RTPStream* stream = getStream(ssrc);
	if (stream == NULL  )
		stream = defaultStream;
	
	
	//Le test ne couvrait que HurryUp : GetRecSSRC dereferencait quand meme.
	//Apres DeleteStreams les deux candidats sont NULL, ce n'est plus theorique.
	if (stream == NULL)
		return 0;
	stream->HurryUp();
	ssrc=stream->GetRecSSRC();	
	//request FIR
	SendFIR(ssrc);

	//packets.Reset();
	/*if (!pendingTMBR)
	{
		//request FIR
		SendFIR();
	} else {
		//Wait for TMBN response to no overflow
		requestFPU = true;
	}*/
	return 0;
}

void RTPSession::SetRTT(DWORD rtt)
{
	//Verrou lecteur : DeleteStreams peut courir en parallele (voir
	//RTPSession::CreateSenderReport).
	ScopedUse scopedStreams(streamUse);
	//Set it
	this->rtt = rtt;
	DWORD recSSRC =0;
	if (defaultStream != NULL)
		recSSRC = defaultStream->GetRecSSRC();
	else
		return;
	
	//if got estimator
	if (remoteRateEstimator)
	{
		//Update estimator
		remoteRateEstimator->UpdateRTT(recSSRC,rtt,getTimeMS());
	}
	{
		std::lock_guard<std::mutex> guard(senderBweMutex);
		senderBWE.UpdateRTT(rtt);
	}

	//Check RTT to enable NACK
	if (useNACK && rtt < 240)
	{
		//Enable NACK only if RTT is small
		isNACKEnabled = true;
		//Update jitter buffer size in ms in [60+rtt,300]
		defaultStream->SetMaxWaitTime(fmin(60+rtt,300));
	} else {
		//Disable NACK
		isNACKEnabled = false;
		//Reduce jitter buffer as we don't use NACK
		defaultStream->SetMaxWaitTime(60);
	}		
}

void RTPSession::onTargetBitrateRequested(DWORD bitrate, bool congestion)
{
    BitrateFeedbackMode mode;
    DWORD announce = 0;
    bool  send;

    //Ce que le pair PRODUIT vraiment : l'amortisseur s'en sert pour reconnaitre
    //une hausse qui ne lui apprendrait rien. Lu hors de notre verrou — il a le
    //sien — et avant lui, pour ne pas imbriquer les deux.
    const DWORD peerBitrate = remoteRateEstimator
                            ? remoteRateEstimator->GetIncomingBitrate() : 0;

    // Memory barrier
    mutex.lock();
    mode = bitrateFeedbackMode;
    bitrateFeedbackThrottler.SetPeerBitrate(peerBitrate);
    //L'amortisseur suit la mesure locale même quand rien ne part : c'est lui qui
    //compose le min() avec un éventuel plafond venu de l'autre patte.
    send = bitrateFeedbackThrottler.OnEstimateChanged(bitrate, getTimeMS(), announce, congestion);
    mutex.unlock();

    Debug("-RTPSession::onTargetBitrateRequested() mode %d, bitrate [%d] -> [%d] send %d for %s stream %p.\n",
	  (int)mode, bitrate, announce, (int)send, MediaFrame::TypeToString(media), this);

    //Feedback SPONTANÉ de l'estimateur : verrouillé par la NÉGOCIATION (arbitrage
    //A2) — pas d'AVPF vers un pair qui n'en a pas demandé. Le chemin explicite
    //(SetMaxReceiveBitrate, contrainte venue de l'aval) ne l'est PAS.
    if (!send || mode == BitrateFeedbackNone)
	return;

    if (mode == BitrateFeedbackREMB)
	SendReceiverEstimatedMaxBitrate(announce);
    else
	SendTempMaxMediaStreamBitrateRequest(announce);
}

int RTPSession::SetMaxReceiveBitrate(DWORD bitrate)
{
	DWORD announce = 0;
	BitrateFeedbackMode mode;
	bool send;

	mutex.lock();
	mode = bitrateFeedbackMode;
	send = bitrateFeedbackThrottler.SetMaxBitrate(bitrate, getTimeMS(), announce);
	mutex.unlock();

	//Rien de neuf à dire au pair.
	if (!send)
		return 0;

	//Contrainte venue de l'aval (relais, consigne négociée) : elle n'est pas
	//verrouillée par la négociation — ce n'est pas une initiative de
	//l'estimateur — mais elle parle le dialecte négocié quand il y en a un.
	//Sans négociation, TMMBR reste le défaut historique de ce chemin.
	return (mode == BitrateFeedbackREMB)
		? SendReceiverEstimatedMaxBitrate(announce)
		: SendTempMaxMediaStreamBitrateRequest(announce);
}

int RTPSession::SendTempMaxMediaStreamBitrateRequest(DWORD bitrate)
{
	//Verrou lecteur : DeleteStreams peut courir en parallele (voir
	//RTPSession::CreateSenderReport).
	ScopedUse scopedStreams(streamUse);
	Debug("-RTPSession::SendTempMaxMediaStreamBitrateRequest [%d] on %s stream\n",bitrate,MediaFrame::TypeToString(media));

	//Create rtcp sender retpor
	RTCPCompoundPacket* rtcp = CreateSenderReport();

	DWORD recSSRC =0;
	if (defaultStream != NULL)
		recSSRC = defaultStream->GetRecSSRC();

	//Create TMMBR
	RTCPRTPFeedback *rfb = RTCPRTPFeedback::Create(RTCPRTPFeedback::TempMaxMediaStreamBitrateRequest,sendSSRC,recSSRC);
	//Limit incoming bitrate
	rfb->AddField( new RTCPRTPFeedback::TempMaxMediaStreamBitrateField(recSSRC,bitrate,0));
	//Add to packet
	rtcp->AddRTCPacket(rfb);

	//We have a pending request : SendSenderReport retransmet le TMMBR tant que
	//le TMMBN du pair n'est pas arrivé (pendingTMBR).
	pendingTMBR = true;
	//Store values
	pendingTMBBitrate = bitrate;

	//Send packet
	int ret = SendPacket(*rtcp);

	//Delete it
	delete(rtcp);

	return ret;
}

RTCPPayloadFeedback* RTPSession::CreateReceiverEstimatedMaxBitrateFeedback(DWORD bitrate)
{
	//Verrou lecteur : DeleteStreams peut courir en parallele (voir
	//RTPSession::CreateSenderReport).
	ScopedUse scopedStreams(streamUse);
	std::list<DWORD> ssrcs;

	//Les flux que l'estimation couvre : ceux que l'estimateur observe, ou à
	//défaut le flux entrant courant — un REMB sans SSRC ne dit pas de qui il
	//parle.
	if (remoteRateEstimator)
		remoteRateEstimator->GetSSRCs(ssrcs);
	if (ssrcs.empty() && defaultStream && defaultStream->GetRecSSRC())
		ssrcs.push_back(defaultStream->GetRecSSRC());

	//SSRC of media source (32 bits) : toujours 0, même convention que le TMMBN
	//de la RFC 5104 §4.2.2.2 — les flux visés sont dans le corps du champ.
	RTCPPayloadFeedback *remb = RTCPPayloadFeedback::Create(RTCPPayloadFeedback::ApplicationLayerFeeedbackMessage,sendSSRC,0);
	remb->AddField(RTCPPayloadFeedback::ApplicationLayerFeeedbackField::CreateReceiverEstimatedMaxBitrate(ssrcs,bitrate));

	return remb;
}

int RTPSession::SendReceiverEstimatedMaxBitrate(DWORD bitrate)
{
	Debug("-RTPSession::SendReceiverEstimatedMaxBitrate [%d] on %s stream\n",bitrate,MediaFrame::TypeToString(media));

	//Create rtcp sender report
	RTCPCompoundPacket* rtcp = CreateSenderReport();

	//Add the REMB. Pas de retransmission armée : REMB n'a pas d'accusé de
	//réception, il se redit au rapport suivant (SendSenderReport).
	rtcp->AddRTCPacket(CreateReceiverEstimatedMaxBitrateFeedback(bitrate));

	//Send packet
	int ret = SendPacket(*rtcp);

	//Delete it
	delete(rtcp);

	return ret;
}

int RTPSession::SendTransportWideFeedback(QWORD nowUs)
{
	if (!transportFeedback.ShouldSend(nowUs))
		return 0;

	TransportWideFeedbackField* field = new TransportWideFeedbackField();
	if (!transportFeedback.BuildFeedback(*field,nowUs))
	{
		delete(field);
		return 0;
	}

	//Paquet RTCP seul, sans rapport d'emission en tete : CreateSenderReport a
	//des effets de bord (fenetre du taux de perte des RR, horodatage du dernier
	//SR pour le RTT) qu'un rapport toutes les 50 ms ruinerait. C'est aussi ce
	//que fait le temoin, qui emet ses rapports en RTCP de taille reduite.
	RTCPRTPFeedback* fb = RTCPRTPFeedback::Create(RTCPRTPFeedback::TransportWideFeedbackMessage,
						      sendSSRC,transportFeedback.GetMediaSSRC());
	fb->AddField(field);

	RTCPCompoundPacket rtcp;
	rtcp.AddRTCPacket(fb);

	if (!transportFeedbackStarted)
	{
		transportFeedbackStarted = true;
		Log("-RTPSession transport-cc feedback started on %s stream %p [ssrc:%x]\n",
		    MediaFrame::TypeToString(media),this,transportFeedback.GetMediaSSRC());
	}

	Debug("-RTPSession transport-cc feedback [base:%u,statuses:%u,interval:%llu ms]\n",
	      field->baseSeq,(DWORD)field->packets.size(),transportFeedback.GetIntervalUs()/1000);

	return SendPacket(rtcp);
}

void RTPSession::ReSendPacket(int seq)
{
	//Verrou lecteur : DeleteStreams peut courir en parallele (voir
	//RTPSession::CreateSenderReport).
	ScopedUse scopedStreams(streamUse);
	//Lock
	if (!useNACK) 
	{
		Debug("Asked to resend RTP seq %d but NACK not enabled.\n", seq);
		return;
	}

	rtxUse.IncUse();
	DWORD recCycles =0;
	if (defaultStream != NULL)
		recCycles = defaultStream->GetRecCycles();
	
	//Calculate ext seq number
	DWORD ext = ((DWORD)recCycles)<<16 | seq;

	//Find packet to retransmit
	RTPOrderedPackets::iterator it = rtxs.find(ext);

	//If we still have it
	if (it!=rtxs.end())
	{
		//Get packet
                DWORD size = MTU;
                BYTE data[MTU+SRTP_MAX_TRAILER_LEN];

                
		RTPTimedPacket* packet = it->second;
                int len = packet->GetSize();

                if (len>size)
		{
                    //Error
                    Error("-RTPSession::ReSendPacket() | not enougth size for copying packet [len:%d]\n",len);
		    return;
		}

               //Copy
                memcpy(data,packet->GetData(),packet->GetSize());
#if 0		// Does not work - recencryption fails
                //If using abs-time
                if (useAbsTime)
                {
                        //Calculate absolute send time field (convert ms to 24-bit unsigned with 18 bit fractional part.
                        // Encoding: Timestamp is in seconds, 24 bit 6.18 fixed point, yielding 64s wraparound and 3.8us resolution (one increment for each 477 bytes going out on a 1Gbps interface).
                        DWORD abs = ((getTimeMS() << 18) / 1000) & 0x00ffffff;
                        //Overwrite it
                        set3(data,sizeof(rtp_hdr_t)+sizeof(rtp_hdr_ext_t)+1,abs);
                }

                //Check if we ar encripted
                if (encript)
                {
                        //Check  session
                        if (!sendSRTPSession)
			{
                                //Error
                                Error("-RTPSession::ReSendPacket() | no sendSRTPSession\n");
				return;
			}
                        //Encript
                        srtp_err_status_t err = srtp_protect(sendSRTPSession,data,&len);
                        //Check error
                        if (err!=srtp_err_status_ok)
                        {
                                //Check if got listener
                                if (auto l = LockListener())
                                        //Request a I frame
                                        l->onFPURequested(this);
                                //Nothing
                                Error("-RTPSession::ReSendPacket() | Error protecting RTP packet [%d] sending intra instead\n",err);
				return;
                        }
                }
                //Check len
#endif
                if (len)
                {
                        Debug("-resent nacked packet ext %d seq %d rtpsess=%p.\n", ext, packet->GetSeqNum(), this);
                        //Send packet
                        sendto(simSocket,data,len,0,sendAddr,sendAddr.Len());
                }
	}
	else
	{
		it = rtxs.begin();
		int first = (it == rtxs.end())? 0 : it->first;

		Debug("-could not resent necket packet seq %d: not in buffer anymore. first seq = %d, cout = %d, rtpsess=%p useNacl=%d\n", 
			ext, first, rtxs.size(), this, useNACK);
                                //Check if got listener
                if (auto l = LockListener())
                        //Request a I frame
                        l->onFPURequested(this);

	}

	//Unlock
	rtxUse.DecUse();
}
int RTPSession::SendTempMaxMediaStreamBitrateNotification(DWORD bitrate,DWORD overhead)
{
	//Verrou lecteur : DeleteStreams peut courir en parallele (voir
	//RTPSession::CreateSenderReport).
	ScopedUse scopedStreams(streamUse);
	Log("-SendTempMaxMediaStreamBitrateNotification [%d,%d]\n",bitrate,overhead);

	//Create rtcp sender retpor
	RTCPCompoundPacket* rtcp = CreateSenderReport();
	
	DWORD recSSRC =0;
	if (defaultStream != NULL)
		recSSRC = defaultStream->GetRecSSRC();
	
	//Create TMMBR
	RTCPRTPFeedback *rfb = RTCPRTPFeedback::Create(RTCPRTPFeedback::TempMaxMediaStreamBitrateNotification,sendSSRC,recSSRC);
	//Limit incoming bitrate
	rfb->AddField( new RTCPRTPFeedback::TempMaxMediaStreamBitrateField(sendSSRC,bitrate,0));
	//Add to packet
	rtcp->AddRTCPacket(rfb);

	//Send packet
	int ret = SendPacket(*rtcp);

	//Delete it
	delete(rtcp);

	//Exit
	return ret;
}


/**
 *  Create a new RTP stream. If the stream already exists, it does nothing
 *  
 *  @param receiving: if this is a receiving or sending stream (only receiving is supported at the moment)
 *  @param ssrc: ssrc of this new stream.
 */
bool RTPSession::AddStream( bool receiving, DWORD ssrc )
{
    bool created = false;
    if ( streamUse.WaitUnusedAndLock(500) )
    {
	RTPStream* stream = getStream(ssrc);
	if ( stream == NULL )
	{
		stream = new RTPStream(this,ssrc);
		
		streams[ssrc]=stream;
		created = true;
	}
        streamUse.Unlock();
	//HORS du verrou streamUse : voir DeleteStreams, meme inversion. Le reveil
	//des GetPacket en attente y est aussi, et pour la meme raison — l'ordre
	//streamUse puis streamWait serait l'inverse de celui de GetPacket.
	if (created)
		OnStreamsChanged();
	if (created && remoteRateEstimator)
		remoteRateEstimator->AddStream(ssrc);
	return true;
    }
    else
    {
        return false;
    }
}

//Reveil seul : aucun objet detruit, donc un thread qui lit encore les streams
//survit a cet appel. C'est ce que DeleteStreams faisait en PREMIERE moitie, et
//c'est la seule moitie utile avant un join.
bool RTPSession::CancelStreams()
{
    streamUse.IncUse();
    for (Streams::iterator it = streams.begin(); it != streams.end(); it++)
    {
	it->second->disabled = true;
        it->second->Cancel();
    }
    streamUse.DecUse();

    //HORS du verrou streamUse, meme inversion que AddStream/DeleteStreams.
    //La boucle ci-dessus ne reveille que les consommateurs de flux EXISTANTS ;
    //celui qui attend la NAISSANCE d'un flux n'est dans aucune de ces entrees.
    //Sur une jambe qui n'a jamais rien recu la map est vide, donc personne
    //n'etait reveille : chaque StopReceiving coutait la borne entiere
    //(ConsumerPollMs) avant que le consommateur ne voie l'arret.
    OnStreamsChanged();
    return true;
}

bool RTPSession::DeleteStreams()
{
    CancelStreams();

    std::list<DWORD> removed;

    streamUse.WaitUnusedAndLock();

    for (Streams::iterator it = streams.begin(); it != streams.end(); it++)
    {
        removed.push_back(it->first);
        delete it->second;
    }

    streams.clear();

    defaultStream = NULL;
    streamUse.Unlock();

    //L'estimateur se previent HORS du verrou streamUse, sinon les deux verrous
    //s'imbriquent dans les DEUX sens et se bloquent : ici streamUse(ecrivain) ->
    //estimateur(ecrivain), et sur le chemin de notification
    //estimateur(lecteur) -> onTargetBitrateRequested -> streamUse(lecteur).
    //L'estimateur n'indexe que des SSRC, jamais nos RTPStream : les detruire
    //avant de l'en avertir ne lui fait rien lire de mort.
    if (remoteRateEstimator)
        for (std::list<DWORD>::iterator it = removed.begin(); it != removed.end(); ++it)
            remoteRateEstimator->RemoveStream(*it);

    return true;
}
/**
 * Set the stream designated by SSRC as the defaut stream, if the stream does not exist create it
 *
 * @param: receiving whether it is receving or sending default stream
 * @param: ssrc: ssrc of the stream to be set as default
 *
 **/
 
bool RTPSession::SetDefaultStream(bool receiving, DWORD ssrc )
{
	AddStream(receiving,ssrc);

	//La map se lit et defaultStream s'ecrit sous le verrou ECRIVAIN : c'est le
	//meme etat que DeleteStreams detruit, et tout le chemin RTCP le lit
	//desormais sous le verrou lecteur.
	streamUse.WaitUnusedAndLock();
	defaultStream = getStream(ssrc);
	streamUse.Unlock();

	//AddStream a deja reveille, mais defaultStream n'etait pas encore pose : un
	//GetPacket sur le flux par defaut aurait vu NULL et redormi.
	OnStreamsChanged();

	return true;
}



/**
 *  get the stream associated to the SSRC .
 */
RTPSession::RTPStream* RTPSession::getStream(DWORD ssrc)
{
	//Find stream
	Streams::iterator it = streams.find(ssrc);

	//If not found
	if (it == streams.end())
		//Error
		return NULL;

	//Get the stream
	return it->second;
}

/**
 *  Change the SSRC of an existing stream.
 */
bool RTPSession::ChangeStream( DWORD oldssrc, DWORD newssrc )
{
	//Cette fonction MUTE la map : verrou ECRIVAIN. Elle n'en prenait aucun,
	//alors que tout le chemin RTCP l'itere desormais sous le verrou lecteur.
	//Appelee depuis onNewStream, ou ReadRTP a deja relache son verrou lecteur.
	streamUse.WaitUnusedAndLock();

	RTPStream* stream = getStream(oldssrc);
	if (stream != NULL)
	{
		streams.erase(oldssrc);
		stream->SetRecSSRC(newssrc);
		streams[newssrc] =stream;
	}

	streamUse.Unlock();

	return stream != NULL;
}

RTCPCompoundPacket* RTPSession::CreateSenderReport()
{
	//streams/defaultStream sont detruits par DeleteStreams (verrou ecrivain) alors
	//que ce thread tourne encore : StopReceiving ne joint PAS le thread
	//RTPSession::Run, et l'estimateur d'un Endpoint fait notifier cette session
	//par le thread RTP d'une AUTRE jambe. Verrou lecteur obligatoire.
	ScopedUse scopedStreams(streamUse);
	timeval tv;

	//Create packet
	RTCPCompoundPacket* rtcp = new RTCPCompoundPacket();

	//Get now
	gettimeofday(&tv, NULL);

	//Create Sender report
	RTCPSenderReport *sr = new RTCPSenderReport();
	
	//Append data
	sr->SetSSRC(sendSSRC );
	sr->SetTimestamp(&tv);
	sr->SetRtpTimestamp(sendLastTime );
	sr->SetOctectsSent(totalSendBytes );
	sr->SetPacketsSent(numSendPackets );

	//Update time of latest sr
	DWORD sinceLastSR = getUpdDifTime(&lastSR);
	
	
	for(Streams::iterator it=streams.begin(); it!=streams.end(); it++)
	{
		RTCPReport *report = NULL;
		
		if (it->second )
		{
			//Create report
			report = it->second->CreateReceiverReport();
		}
		
		if (report != NULL)
		{
			//Append it
			sr->AddReport(report);
		}
		
	}
	

	//Append SR to rtcp
	rtcp->AddRTCPacket(sr);

	//Store last send SR 32 middle bits
	SetSendSR(sr->GetNTPSec() << 16 | sr->GetNTPFrac() >> 16);

	//Create SDES
	RTCPSDES* sdes = new RTCPSDES();

	//Create description
	RTCPSDES::Description *desc = new RTCPSDES::Description();
	//Set ssrc
	desc->SetSSRC(sendSSRC);
	//Add item
	desc->AddItem( new RTCPSDES::Item(RTCPSDES::Item::CName,cname ));

	//Add it
	sdes->AddDescription(desc);

	//Add to rtcp
	rtcp->AddRTCPacket(sdes);

	//Return it
	return rtcp;
}

int RTPSession::SendSenderReport()
{
	//Verrou lecteur : DeleteStreams peut courir en parallele (voir
	//RTPSession::CreateSenderReport).
	ScopedUse scopedStreams(streamUse);
	//Create rtcp sender retpor
	RTCPCompoundPacket* rtcp = CreateSenderReport();
	DWORD recSSRC =0;
	if (defaultStream != NULL)
		recSSRC = defaultStream->GetRecSSRC();
		
	//If we have not got a notification from latest TMBR
	if (pendingTMBR )
	{
		//Resend TMMBR
		RTCPRTPFeedback *rfb = RTCPRTPFeedback::Create(RTCPRTPFeedback::TempMaxMediaStreamBitrateRequest,sendSSRC ,recSSRC);
		//Limit incoming bitrate
		rfb->AddField( new RTCPRTPFeedback::TempMaxMediaStreamBitrateField(recSSRC,pendingTMBBitrate,0));
		//Add to packet
		rtcp->AddRTCPacket(rfb);
	} else if (remoteRateEstimator && bitrateFeedbackMode != BitrateFeedbackNone )	{
		//Get lastest estimation, bornée par un éventuel plafond venu de l'autre
		//patte : cette répétition périodique ne doit pas défaire ce que
		//SetMaxReceiveBitrate vient d'annoncer. Compose() est sans effet de bord
		//— c'est une redite, pas une nouvelle décision.
		DWORD estimation = bitrateFeedbackThrottler.Compose(remoteRateEstimator->GetEstimatedBitrate());
		//If it was ok
		if (estimation && estimation != RembThrottler::NoLimit)
		{
			//Le mode TMMBR émet les deux dialectes (comportement historique) ;
			//le mode REMB n'émet que le REMB.
			if (bitrateFeedbackMode == BitrateFeedbackTMMBR)
			{
				//Resend TMMBR
				RTCPRTPFeedback *rfb = RTCPRTPFeedback::Create(RTCPRTPFeedback::TempMaxMediaStreamBitrateRequest,sendSSRC,recSSRC);
				//Limit incoming bitrate
				rfb->AddField( new RTCPRTPFeedback::TempMaxMediaStreamBitrateField(recSSRC,estimation,0));
				//Add to packet
				rtcp->AddRTCPacket(rfb);
			}
			//Add to packet
			rtcp->AddRTCPacket(CreateReceiverEstimatedMaxBitrateFeedback(estimation));
			Debug("SR: reporting estimated bandwidth of %d to %s\n", estimation,  sendRtcpAddr.Address().ToString().c_str());
		}
	}
	
	//Send packet
	int ret = SendPacket(*rtcp);

	//Delete it
	delete(rtcp);

	//Exit
	return ret;
}


// Default behavior
void RTPSession::Listener::onNewStream( RTPSession *session, DWORD newSsrc, bool receiving )
{
	if ( ! receiving) return;
	
	
	DWORD oldssrc = session->GetDefaultStream(true);
	
	if ( oldssrc )
	{
		session->ChangeStream( oldssrc, newSsrc );
	}
	else
	{
		session->SetDefaultStream( true, newSsrc );
	}
}

bool RTPSession::RTPStream::Add(RTPTimedPacket *packet, DWORD size)
{

	//Get sec number
	WORD seq = packet->GetSeqNum();
	
	//Check if we have a sequence wrap
	if (seq == 0)
	{
		Log("-RTPSession: we should have a seq wrap. lastSeq=0x%0x, current cycle = %d\n", recExtSeq, recCycles);
	}
	
	if ( seq<0x00FF && (recExtSeq & 0xFFFF) > 0xFF00U)
	{
		//Increase cycles
		recCycles++;
		Log("-RTPSession: seqno wrap. lastSeq=0x%0x, NEW cycle = %d\n", recExtSeq, recCycles);
	}
	//Set cycles
	packet->SetSeqCycles(recCycles);

	//If remote estimator
	if ( s->GetRemoteRateEstimator() )
		//Update rate control
		s->GetRemoteRateEstimator()->Update(recSSRC,packet);

	//Increase stats
	numRecvPackets++;
	totalRecvPacketsSinceLastSR++;
	totalRecvBytes += size;
	totalRecvBytesSinceLastSR += size;
        recCodec = packet->GetCodec();

	//Get ext seq
	DWORD extSeq = packet->GetExtSeqNum();

	//Check if it is the min for this SR
	if (extSeq<minRecvExtSeqNumSinceLastSR)
		//Store minimum
		minRecvExtSeqNumSinceLastSR = extSeq;

	//If we have a not out of order pacekt
	if (extSeq>recExtSeq)
	{
		//Check possible lost pacekts
		if (recExtSeq && extSeq>(recExtSeq+1))
		{
			//Get number of lost packets
			WORD lost = extSeq-recExtSeq-1;
			//Base packet missing
			WORD base = recExtSeq+1;

			//If remote estimator
			if ( s->GetRemoteRateEstimator() )
				//Update estimator
				s->GetRemoteRateEstimator()->UpdateLost(recSSRC,lost, getTimeMS());

			//If nack is enable t waiting for a PLI/FIR response (to not oeverflow)
			if (s->IsNACKEnabled() && !s->IsRequestFPU())
			{
				//Double check
				if (lost<0x0FFF)
				{				
				Log("-Nacking %d lost %d\n",base,lost);

				//Generate new RTCP NACK report
				RTCPCompoundPacket* rtcp = new RTCPCompoundPacket();

				//Send them
				while (lost>0)
				{
					//Skip base
					lost--;
					//Get number of lost in the 16 mask
					WORD n = lost;
					//Check nex 16 packets
					if (n>16)
						//Clip
						n = 16;
					//Create mask
					WORD mask = 0xFFFF << (16-n);
					//Create NACK
					RTCPRTPFeedback *nack = RTCPRTPFeedback::Create(RTCPRTPFeedback::NACK,s->GetSendSSRC(),recSSRC);
					//Limit incoming bitrate
					nack->AddField( new RTCPRTPFeedback::NACKField(base,mask));
					//Add to packet
					rtcp->AddRTCPacket(nack);
					//Reduce lost
					lost -= n;
					//Increase base
					base += 16;
				}

				//Send packet
				s->SendPacket(*rtcp);

				//Delete it
				delete(rtcp);
			} else {
					Error("-Weird lost count [lost:%d,base:%d,recExtSeq:%d,recCycles:%d,extSeq:%d,seq:%d]\n",lost,base,recExtSeq,recCycles,extSeq,packet->GetSeqNum());
				}
			}
		}
		
		//Update seq num
		recExtSeq = extSeq;

		//Get diff from previous
		DWORD diff = getUpdDifTime(&recTimeval)/1000;

		//If it is not first one and not from the same frame
		if (recTimestamp && recTimestamp<packet->GetClockTimestamp())
		{
			//Get difference of arravail times
			int d = (packet->GetClockTimestamp()-recTimestamp)-diff;
			//Check negative
			if (d<0)
				//Calc abs
				d = -d;
			//Calculate variance
			int v = d-jitter;
			//Calculate jitter
			jitter += v/16;
		}

		//Update rtp timestamp
		recTimestamp = packet->GetClockTimestamp();
	}

	//Check if we are using fec
	if (s->UseFEC())
	{
		//Append to the FEC decoder
		if (fec.AddPacket(packet))
			//Append only packets with media data
			RTPBuffer::Add(packet);
		//Try to recover
		RTPTimedPacket* recovered = fec.Recover();
		//If we have recovered a pacekt
		while(recovered)
		{
			//Log
			Log("packet recovered!!!!\n");
			//Overwrite time with the time of the original 
			recovered->SetTime(packet->GetTime());
			//Get pacekte type
			BYTE t = recovered->GetType();
			//Map receovered data codec
			BYTE c = s->GetRtpMapIn()->GetCodecForType(t);
			//Check codec
			if (c!=RTPMap::NotFound)
				//Set codec
				recovered->SetCodec(c);
			else
				//Set type for passtrhought
				recovered->SetCodec(t);
			//add recovered packet
			RTPBuffer::Add(recovered);
			//Try to recover another one (yuhu!)
			recovered = fec.Recover();
		}
	} 
	else 
	{
		//Add packet
		RTPBuffer::Add(packet);
	}
	timeval lastSR = s->GetLastSR();
	
	//Check if we need to send SR
	if (isZeroTime(&lastSR) || getDifTime(&lastSR)>2000000)
		//Send it
		s->SendSenderReport();

	return true;
}

RTCPReport* RTPSession::RTPStream::CreateReceiverReport()
{

	//Create report
	RTCPReport *report = NULL;
	//If we have received somthing
	timeval lastReceivedSR = s->GetLastReceivedSR();
	
	if (totalRecvPacketsSinceLastSR && recExtSeq>=minRecvExtSeqNumSinceLastSR)
	{

		//Get number of total packtes
		DWORD total = recExtSeq - minRecvExtSeqNumSinceLastSR + 1;
		//Calculate lost
		DWORD lostRecvPacketsSinceLastSR = total - totalRecvPacketsSinceLastSR;
		//Add to total lost count
		lostRecvPackets += lostRecvPacketsSinceLastSR;
		//Calculate fraction lost
		DWORD frac = (lostRecvPacketsSinceLastSR*256)/total;

		//Create report
		report = new RTCPReport();

		//Set SSRC of incoming rtp stream
		report->SetSSRC(recSSRC);

		//Check last report time
		if (!isZeroTime(&lastReceivedSR))
			//Get time and update it
			report->SetDelaySinceLastSRMilis(getDifTime(&lastReceivedSR)/1000);
		else
			//No previous SR
			report->SetDelaySinceLastSRMilis(0);
		//Set data
		report->SetLastSR(s->GetRecSR() );
		report->SetFractionLost(frac);
		report->SetLastJitter(jitter);
		report->SetLostCount(lostRecvPackets);
		report->SetLastSeqNum(recExtSeq);

		//Reset data
		totalRecvPacketsSinceLastSR = 0;
		totalRecvBytesSinceLastSR = 0;
		minRecvExtSeqNumSinceLastSR = RTPPacket::MaxExtSeqNum;

		
	}
	return report;
}

void RTPSession::onDTLSSetup(DTLSConnection::Suite suite,BYTE* localMasterKey,DWORD localMasterKeySize,BYTE* remoteMasterKey,DWORD remoteMasterKeySize)
{
	Log("-onDTLSSetup for [%s]\n",MediaFrame::TypeToString(media));

	switch (suite)
	{
		case DTLSConnection::AES_CM_128_HMAC_SHA1_80:
			//Set keys
			SetLocalCryptoSDES("AES_CM_128_HMAC_SHA1_80",localMasterKey,localMasterKeySize);
			SetRemoteCryptoSDES("AES_CM_128_HMAC_SHA1_80",remoteMasterKey,remoteMasterKeySize, 0);
			break;
		case DTLSConnection::AES_CM_128_HMAC_SHA1_32:
			//Set keys
			SetLocalCryptoSDES("AES_CM_128_HMAC_SHA1_32",localMasterKey,localMasterKeySize);
			SetRemoteCryptoSDES("AES_CM_128_HMAC_SHA1_32",remoteMasterKey,remoteMasterKeySize, 0);
			break;
		case DTLSConnection::F8_128_HMAC_SHA1_80:
			//Set keys
			SetLocalCryptoSDES("NULL_CIPHER_HMAC_SHA1_80",localMasterKey,localMasterKeySize);
			SetRemoteCryptoSDES("NULL_CIPHER_HMAC_SHA1_80",remoteMasterKey,remoteMasterKeySize, 0);
			break;
	}
}

bool RTPSession::GetStatistics( DWORD ssrc, MediaStatistics & stats)
{
	//Verrou lecteur : DeleteStreams peut courir en parallele (voir
	//RTPSession::CreateSenderReport).
	ScopedUse scopedStreams(streamUse);
     memset(&stats, 0, sizeof(stats));
     
    if (! running || rtpMapOut == NULL)
    {
        return false;
    }
		
    stats.numSendPackets = numSendPackets;
    stats.totalSendBytes = totalSendBytes;
	
	if (rtpMapOut->find(sendType) != rtpMapOut->end() )
		stats.sendingCodec = (*rtpMapOut)[sendType];
	else
		stats.sendingCodec = -1;
		
    RTPStream * s =(ssrc > 0) ? getStream(ssrc) : NULL;
    if (s != NULL)
    {
        stats.numRecvPackets = s->GetNumRecvPackets();
        stats.totalRecvBytes = s->GetTotalRecvBytes();
        stats.bwIn = 0;
        stats.lostRecvPackets = s->GetLostRecvPackets();
        stats.receivingCodec = s->GetRecCodec();
    }
    else
    {
        for (Streams::iterator it = streams.begin(); it != streams.end(); it++)
        {
            s = it->second;
			if (s != NULL)	
            {
				stats.numRecvPackets += s->GetNumRecvPackets();
				stats.totalRecvBytes += s->GetTotalRecvBytes();
				stats.bwIn = 0;
				stats.lostRecvPackets += s->GetLostRecvPackets();
			}
        }
        if (defaultStream != NULL)
            stats.receivingCodec = defaultStream->GetRecCodec();
    }
    return true;


}

void RTPSession::ProcessTransportWideFeedback(RTCPRTPFeedback* fb)
{
	bool changed = false;
	DWORD estimate = 0;
	{
		std::lock_guard<std::mutex> guard(senderBweMutex);
		for (BYTE i=0;i<fb->GetFieldCount();i++)
		{
			TransportWideFeedbackField* field = (TransportWideFeedbackField*)fb->GetField(i);
			DWORD lost = 0, unknown = 0;
			std::vector<SentPacketHistory::Result> results = sentHistory.ProcessFeedback(*field, lost, unknown);
			changed |= senderBWE.ProcessFeedback(results, lost, getTime());
		}
		estimate = senderBWE.GetEstimatedBitrate();
	}
	//Nos rapports voyagent sur le meme lien montant que notre media : leur
	//cadence se cale sur ce qu'il porte (temoin, 5 % du debit d'emission).
	if (estimate)
		transportFeedback.SetSendBitrate(estimate);
	//Notification HORS du verrou : le listener peut rappeler la session
	if (changed && estimate)
		if (auto l = LockListener())
			l->onSenderEstimatedBitrate(this, estimate);
}

void RTPSession::OnReportedLoss(BYTE fractionLost)
{
	bool changed;
	DWORD estimate;
	{
		std::lock_guard<std::mutex> guard(senderBweMutex);
		changed = senderBWE.UpdateFractionLost(fractionLost, getTime());
		estimate = senderBWE.GetEstimatedBitrate();
	}
	if (changed && estimate)
		if (auto l = LockListener())
			l->onSenderEstimatedBitrate(this, estimate);
}
