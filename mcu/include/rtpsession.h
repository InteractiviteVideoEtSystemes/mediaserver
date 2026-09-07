#ifndef _RTPSESSION_H_
#define _RTPSESSION_H_
#include "pollhandler.h"
#include "wait.h"
#include <sys/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <mutex>
#include <memory>
#include <map>
#include <string>
#include <poll.h>
#include <srtp2/srtp.h>
#include "config.h"
#include "use.h"
#include "rtp.h"
#include "senderbwe.h"
#include "rtpbuffer.h"
#include "remoteratecontrol.h"
#include "fecdecoder.h"
#include "medkit/stunmessage.h"
#include "remoterateestimator.h"
#include "rembthrottler.h"
#include "dtls.h"
#include <atomic>

#include "ipaddress.h"
#include "addressprofiles.h"



class RtpSessionSet;

class RTPSession : 
	public RemoteRateEstimator::Listener,
	public DTLSConnection::Listener,
	public PollHandler
{
public:
	class Listener
	{
	public:
		//Virtual desctructor
		virtual ~Listener(){};
	public:
		//Interface
		virtual void onFPURequested(RTPSession *session) = 0;
		virtual void onReceiverEstimatedMaxBitrate(RTPSession *session,DWORD bitrate) = 0;
		virtual void onTempMaxMediaStreamBitrateRequest(RTPSession *session,DWORD bitrate,DWORD overhead) = 0;
		virtual void onNewStream( RTPSession *session, DWORD newSsrc, bool receiving ) ;
		//Watchdog d'inactivité RTP : appelé UNE seule fois lorsqu'aucun paquet n'a
		//été reçu depuis plus de rtpTimeout ms (gap 5 - EndpointDisconnectedEvent).
		//Non pur pour ne pas casser les Listener existants.
		virtual void onRTPTimeout( RTPSession *session ) {}
		//P5 : appelé UNE seule fois par cycle de réception, au premier paquet RTP/SRTP
		//reçu et validé (déchiffrement OK => DTLS terminé, ou pas de crypto). Ré-armé
		//via ArmRTPReceivedNotification(). Non pur (Listener existants inchangés).
		virtual void onRTPPacketReceived( RTPSession *session ) {}
		//Lot 6.3 : l'estimateur émetteur LOCAL a une nouvelle cible pour ce
		//flux sortant (bps). À composer par min() avec la limite du pair
		//(REMB/TMMBR reçu), jamais à écraser. Non pur (Listener inchangés).
		virtual void onSenderEstimatedBitrate( RTPSession *session, DWORD bitrate ) {}
	};

	//Ce que la session émet comme feedback de débit vers le pair, posé par la
	//NÉGOCIATION (propriétés "tmmbr"/"remb", cf. SetProperties) et par elle
	//seule : un pair qui n'a pas demandé d'AVPF n'en reçoit pas. TMMBR
	//(RFC 5104) est le dialecte SIP, REMB (draft-alvestrand-rmcat-remb-03)
	//celui des navigateurs, qui n'offrent que "goog-remb". Le mode TMMBR émet
	//les deux — c'est le comportement historique, et un pair qui comprend
	//TMMBR ne perd rien à recevoir aussi le REMB.
	enum BitrateFeedbackMode
	{
		BitrateFeedbackNone	= 0,
		BitrateFeedbackREMB	= 1,
		BitrateFeedbackTMMBR	= 2
	};

public:

	static bool SetPortRange(int minPort, int maxPort);
	static DWORD GetMinPort() { return minLocalPort; }
	static DWORD GetMaxPort() { return maxLocalPort; }

	//Adresse annoncée dans le SDP (ligne c= et candidats ICE). Elle est globale au
	//serveur : la tenir ici, à côté de la plage de ports, évite que chaque API de
	//contrôle (JSR-309 via Endpoint::GetMediaCandidates, MCU via StartReceiving) la
	//redérive à sa façon. Derrière un NAT elle diffère de l'IP bindée, d'où le
	//réglage explicite --public-ip.
	//SetAnnouncedIp : à appeler au démarrage, avant tout GetAnnouncedIp. Rend true
	//si une adresse explicite valide a été installée, false si elle est absente ou
	//invalide (l'auto-détection reste alors en place).
	static bool SetAnnouncedIp(const char* ip);
	//Résolue à la première utilisation si aucune adresse explicite n'a été fournie :
	//premier IPv4 non loopback de l'hôte. Chaîne vide si l'hôte ne se résout pas.
	static const char* GetAnnouncedIp();

private:
	// Admissible port range
	static DWORD minLocalPort;
	static DWORD maxLocalPort;
	// Adresse annoncée dans le SDP, vide si la résolution a échoué. Le drapeau
	// distingue « pas encore résolue » de « résolue, sans résultat » : un échec
	// mémorisé évite de relancer un gethostbyname perdant à chaque appel.
	static std::string announcedIp;
	static bool announcedIpResolved;
	static std::mutex announcedIpMutex;
	
public:
	RTPSession(MediaFrame::Type media,Listener *listener, MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	~RTPSession();
	//M-6 : enregistrement différé d'un listener géré par shared_ptr
	//(RTPParticipant). Une fois appelé, prend le pas sur le pointeur brut du
	//constructeur.
	void SetWeakListener(std::weak_ptr<Listener> l) { weakListener = std::move(l); hasWeakListener = true; }
	int Init();
	//Adresse à lier par les sockets média, AVANT Init : c'est elle qui décide de
	//l'interface empruntée, donc du profil d'adressage effectif de cette jambe
	//(voir NETWORK-CONFIGURATION.md). Vide (le défaut) = écoute dual-stack sur
	//interfaces, comportement historique. Rend false si l'adresse est
	//inutilisable ; la session reste alors sur le défaut.
	bool SetBindAddress(const IPAddress& addr);
	const IPAddress& GetBindAddress() const { return bindAddress; }

	//Profil d'adressage de CETTE jambe (NETWORK-CONFIGURATION.md) : le contrôleur le
	//demande dans StartSending/StartReceiving, le serveur en tire l'adresse à
	//lier — donc l'interface — et l'adresse à annoncer.
	//
	//Le profil se fixe UNE FOIS : le second appel, s'il en porte un différent,
	//est un ÉCHEC et non une reconfiguration silencieuse. En RTP symétrique la
	//socket est la même dans les deux sens, et changer d'adresse en cours
	//d'appel voudrait dire la relier sous le média — le port publié dans le SDP
	//changerait sans que le pair en sache rien.
	//
	//`profile` : "publicv4" | "publicv6" | "internalv4" | "internalv6", ou NULL
	/// chaîne vide pour « le profil par défaut », c'est-à-dire le comportement
	//d'un contrôleur qui ignore cette notion. Rend false et remplit `error` si
	//le nom est inconnu, le profil indisponible, ou déjà fixé à un autre.
	bool SetAddressProfile(const char* profile, std::string& error);

	//Adresse à publier dans le SDP pour cette jambe : celle du profil retenu
	//(NATée s'il y a lieu). Vide si aucun profil n'est disponible.
	IPAddress GetAnnouncedAddress() const;
	void SetRemoteRateEstimator(RemoteRateEstimator* estimator);
	int SetLocalPort(int recvPort);
	int GetLocalPort();
	int SetRemotePort(char *ip,int sendPort);
	int 					GetRemotePort();
	//Configure UNIQUEMENT le seuil d'inactivité RTP en ms (n'arme pas le watchdog).
	//Le chrono ne démarre qu'à l'armement explicite (ArmRTPTimeout), tenu par le
	//contrôleur au moment du SDP answer (gap 5).
	void SetRTPTimeout(DWORD timeout) { rtpTimeout = timeout; }
	//Arme/désarme le watchdog d'inactivité RTP. À appeler par le contrôleur (elixip)
	//juste après l'envoi du SDP answer : le chrono part de cet instant, ce qui évite
	//les timeouts intempestifs pendant la sonnerie ET détecte « répondu mais aucun
	//média reçu ». timeoutMs>0 : (re)configure le seuil + arme ; timeoutMs==0 : désarme.
	void ArmRTPTimeout(DWORD timeoutMs);
	//P5 : (ré)arme la notification « premier paquet RTP/SRTP reçu » (onRTPPacketReceived).
	//Appelé à chaque (re)démarrage de la réception pour ré-émettre l'événement de connexion.
	void ArmRTPReceivedNotification() { rtpReceivedNotified = false; }
	//P6 : amorce le NAT traversal symétrique (comedia). Émet une courte rafale de
	//paquets RTP valides vers la destination pour ouvrir le pinhole et faire latcher
	//un pair symétrique (Asterisk nat=yes) qui n'émet qu'après avoir reçu du média.
	//Réservé au RTP en clair (le WebRTC amorce via STUN/DTLS). Appelé quand la
	//destination d'envoi devient connue (SetRemotePort) ; le thread Run cadence les
	//paquets suivants (~20 ms). No-op si chiffré ou destination inconnue.
	void ArmNATPriming();
	//Trickle ICE Niveau 1 (gap 1) : parse une ligne SDP "candidate:..." et, pour un
	//candidat host/srflx de priorité supérieure au candidat courant, reconfigure la
	//cible d'envoi RTP/RTCP. Retourne 1 si accepté/ignoré proprement, 0 sur erreur.
	int AddICECandidate(const char* candidate);
	
	
	RemoteRateEstimator* 	GetRemoteRateEstimator() 	{	return remoteRateEstimator; };
	BitrateFeedbackMode	GetBitrateFeedbackMode()	{	return bitrateFeedbackMode; };
	bool 			IsNACKEnabled() 		{	return isNACKEnabled; }
	bool 			IsRequestFPU() 			{	return requestFPU; };
	bool 			UseFEC()			{	return useFEC; };
	bool 			UseExtFIR()			{	return useExtFIR; };
	bool 			UseRtcpFIR()			{	return useRtcpFIR; };
	int End();

	/**
	 *  Create a new RTP stream. If the stream already exists, it does nothing
	 *  
	 *  @param receiving: if this is a receiving or sending stream (only receiving is supported at the moment)
	 *  @param ssrc: ssrc of this new stream.
	 */
	bool AddStream( bool receiving, DWORD ssrc );
	
	/**
	 *  Get the default RTP stream SSRC of this session. It is the stream that is automatically created.
	 */
	DWORD GetDefaultStream(bool receiving) { return (defaultStream != NULL) ? defaultStream->GetRecSSRC() : 0 ; }


        bool DeleteStreams();
	/**
	 * Reveille les lecteurs des streams (disabled + Cancel) SANS rien detruire.
	 * A appeler avant de joindre un thread qui lit les streams : DeleteStreams
	 * fait les deux, donc l'utiliser pour reveiller detruit trop tot.
	 **/
	bool CancelStreams();
	/**
	 * Set the stream designated by SSRC as the defaut stream, if the stream does not exist create it
	 *
	 * @param: receiving whether it is receving or sending default stream
	 * @param: ssrc: ssrc of the stream to be set as default
	 *
	 **/
	bool SetDefaultStream(bool receiving, DWORD ssrc );
	

	/**
	 *  Change the SSRC of an existing stream.
	 */
	bool ChangeStream( DWORD oldssrc, DWORD newssrc );

	void SetSendingRTPMap(RTPMap &map);
	void SetReceivingRTPMap(RTPMap &map);
	bool SetSendingCodec(DWORD codec);
	//Sonde silencieuse : le codec est-il dans la rtpMap de sortie ? Ne bascule
	//rien et ne journalise rien. C'est le chemin nominal de l'arbitrage pont
	//(TryCodec), qui échoue par construction dès que les deux pattes ne portent
	//pas le même codec — le journaliser en Error alarmait la supervision pour
	//rien (recette 2026-08-14).
	bool CanSendCodec(DWORD codec);

	int ForwardPacket( RTPPacket &packet, DWORD recssrc );
	
	int SendEmptyPacket();
	int SendPacket(RTPPacket &packet,DWORD timestamp);
	int SendPacket(RTPPacket &packet);
	
	
	void CancelGetPacket();
	
	//Période de relecture du drapeau d'arrêt par un consommateur, donc la borne
	//qu'il passe à GetPacket. Assez courte pour qu'un StopReceiving réponde,
	//assez longue pour que l'attente ne coûte rien.
	static const DWORD ConsumerPollMs = 200;

	//Bornes de l'estimateur d'ÉMISSION d'une session vidéo (arbitrage
	//mainteneur 2026-09-02). Plancher : sous 128 kb/s une consigne vidéo n'est
	//plus une régulation. Plafond : sans transport-cc l'estimateur monte de
	//+8 %/s tant qu'il n'y a pas de perte, jusqu'au défaut de 30 Mb/s — un
	//chiffre qui ne veut rien dire et qui rend la redescente lente. 6 Mb/s
	//couvre du 1080p de bonne qualité. RÈGLE : ce plafond doit rester
	//SUPÉRIEUR à la plus haute consigne vidéo négociée, sinon c'est lui qui
	//bride l'encodeur, en silence, sur toute jambe sans transport-cc.
	static const DWORD VideoSenderEstimateMinBps = 128000;
	static const DWORD VideoSenderEstimateMaxBps = 6000000;

	// Multi stream
	//`timeoutMs` borne l'attente ; `ssrc` 0 = flux par défaut. Rend NULL sur
	//expiration, sur annulation, ou quand le flux demandé n'existe pas encore —
	//dans ce dernier cas APRÈS avoir attendu sa naissance, jamais en sondant.
	RTPPacket* GetPacket(DWORD ssrc, DWORD timeoutMs);
	void CancelGetPacket(DWORD & ssrc);
	
	void ResetPacket(bool clear) { if (defaultStream != NULL) defaultStream->Reset(clear) ;};
	void ResetPacket(DWORD & ssrc, bool clear);
	
        /**
         * Obtain the statistcs for a given stream or all the streams
         * @param ssrc SSRC of the receiving stream, 0 to sum up all the streams
         * @param stats statistic structure to populate
         * @return  true if the stats coulf be gathered, false in case of error
         */
        bool GetStatistics( DWORD ssrc, MediaStatistics & stats);


	DWORD 	GetSendSSRC()			const { return sendSSRC;	}
	const RTPMap* GetRtpMapIn()			const  { return rtpMapIn;	}
	timeval GetLastSR()				const  { return lastSR;	}
	timeval GetLastReceivedSR()		const  { return lastReceivedSR;	}
	//DWORD 	GetSendLastTime()		const  { return sendLastTime;	}
	DWORD 	GetRecSR()				const  { return recSR;	}
	//DWORD 	GetSendSR()				const  { return sendSR;	}
	
	//bool	GetPendingTMBR()			const  { return pendingTMBR;	}
	//DWORD	GetPendingTMBBitrate()	const  { return pendingTMBBitrate;	}
	
	//char*	GetCname()				const  { return cname;	}
	
	void 	SetSendSR(DWORD sendsr)				{ sendSR=sendsr;	}
	
	MediaFrame::Type 		GetMediaType()	const { return media;		}
	MediaFrame::MediaRole 	GetMediaRole()	const { return role;		}

	//Nom lisible de la patte qui porte cette session (le `name` de l'Endpoint
	//JSR-309, p.ex. "cx-66-outbound"). Purement descriptif : il ne sert qu'aux
	//traces. Sans lui, une erreur de réception ne dit ni de quel appel ni de
	//quelle jambe elle vient — et un B2BUA en a deux par appel, sur le même
	//processus et le même log.
	void SetLabel(const std::wstring& l)	{ label = l;			}
	const std::wstring& GetLabel()	const	{ return label;			}
	//Ce que les traces impriment : une session sans patte (un test, un chemin qui
	//n'est pas passé par un Endpoint) doit se lire comme telle plutôt que laisser
	//un blanc entre deux virgules.
	const wchar_t* LabelForLog()	const	{ return label.empty() ? L"unnamed" : label.c_str(); }

	int SetLocalCryptoSDES(const char* suite, const char* key64);
	int SetRemoteCryptoSDES(const char* suite, const char* key64,int keyRank=0);
	int SetRemoteCryptoDTLS(const char *setup,const char *hash,const char *fingerprint);

	//Consommateur des données APPLICATIVES du DTLS, et sa cadence. C'est par là
	//qu'un data channel WebRTC (RFC 8831) se greffe : la session reste le porteur
	//— ICE, DTLS, socket, latch d'adresse, thread poll — et ne connaît ni SCTP ni
	//T.140. Elle livre des octets et bat la mesure, rien de plus.
	class ApplicationListener : public DTLSConnection::ApplicationListener
	{
	public:
		//Période d'appel de onApplicationTick, en ms. 0 (défaut) = pas de
		//cadence. Une pile SCTP en espace utilisateur n'a pas de thread : ses
		//retransmissions et ses heartbeats sont battus par la boucle poll de la
		//session, ce qui garde tout le chemin sur un seul thread — l'objet SSL
		//n'est pas concurrent.
		virtual DWORD GetApplicationTickMs() { return 0; }
		//Écoulement RÉEL depuis le dernier appel : le poll rend la main plus tôt
		//sur un paquet entrant, plus tard sous charge.
		virtual void  onApplicationTick(DWORD elapsedMs) {}
	};

	//À poser AVANT Init, qui démarre la boucle. NULL retire le consommateur, qui
	//doit alors le faire avant de mourir : la session ne le possède pas.
	void SetDTLSApplicationListener(ApplicationListener* listener);
	//Chiffre et émet un bloc applicatif vers le pair latché. À n'appeler QUE
	//depuis le thread de la session : l'objet SSL n'est pas concurrent, et cette
	//boucle le lit à chaque datagramme entrant.
	int  SendDTLSApplicationData(const BYTE* data,DWORD size);
	//Réveille le réacteur qui bat cette session, sans attendre un paquet entrant.
	//Un porteur de data channel en a besoin : la pile SCTP a produit des
	//datagrammes, et c'est ce thread — le seul qui ait le droit de chiffrer — qui
	//les vide.
	void WakeUp();

	//Réacteur qui bat cette session, à poser AVANT Init. Refusé après, parce que
	//End() doit se retirer du groupe où Init l'a inscrite, et pas d'un autre.
	//Jamais posé = réacteur par défaut du processus (RtpSessionSet::Default).
	bool SetPollGroup(RtpSessionSet* group);
	//Le canal applicatif est-il ouvert ? Une jambe sans RTP n'a pas de profil
	//SRTP, donc onDTLSSetup ne lui dit jamais rien : c'est ainsi qu'un porteur de
	//data channel sait qu'il peut commencer à écrire.
	bool IsDTLSHandshakeCompleted() const { return dtls.IsHandshakeCompleted(); }

	int SetLocalSTUNCredentials(const char* username, const char* pwd);
	int SetRemoteSTUNCredentials(const char* username, const char* pwd);
	int SetProperties(const Properties& properties);
	int RequestFPU();
	int RequestFPU(DWORD & ssrc);
	//Acquittement positif d'une trame de référence décodée (RPSI, RFC 4585
	//§6.3.3) : c'est ce qui évite à un émetteur msvp8 de forcer une trame clé
	//toutes les 3 s. pictureId = la valeur telle que reçue dans le payload
	//descriptor (RFC 7741 §5.1, VP9 identique) : bit 0x8000 posé -> bit
	//string de 2 octets réseau, sinon 1 octet. ssrc=0 -> flux par défaut.
	int SendReferencePictureSelectionIndication(DWORD ssrc, WORD pictureId);
	
	int SendTempMaxMediaStreamBitrateNotification(DWORD bitrate,DWORD overhead);
	//Envoie un TMMBR au pair (borne son débit d'émission, en bps) et arme la
	//retransmission : SendSenderReport le répète tant que le TMMBN n'est pas
	//arrivé (pendingTMBR). Chemin EXPLICITE (demande relayée du mode pont,
	//consigne négociée) — non verrouillé par la propriété "tmmbr", qui ne
	//gouverne que le feedback spontané de l'estimateur.
	int SendTempMaxMediaStreamBitrateRequest(DWORD bitrate);
	//Annonce au pair le débit qu'on estime pouvoir recevoir de lui (REMB,
	//draft-alvestrand-rmcat-remb-03). Même rôle que le TMMBR ci-dessus dans un
	//autre dialecte, sans retransmission : REMB n'a pas d'accusé, il se redit
	//simplement au rapport suivant.
	int SendReceiverEstimatedMaxBitrate(DWORD bitrate);
	//Plafond de débit posé de l'EXTÉRIEUR (l'autre patte d'un relais) : composé
	//par min() avec l'estimation locale dans l'amortisseur, et annoncé au pair
	//dans le dialecte négocié. Rend 0 si rien n'est parti (rien de neuf à dire).
	int SetMaxReceiveBitrate(DWORD bitrate);

	virtual void onTargetBitrateRequested(DWORD bitrate, bool congestion);
	virtual void onDTLSSetup(DTLSConnection::Suite suite,BYTE* localMasterKey,DWORD localMasterKeySize,BYTE* remoteMasterKey,DWORD remoteMasterKeySize);
private:
	//Le champ REMB (identifiant 'REMB' + débit + SSRC couverts) prêt à être
	//ajouté à un paquet composé. Un seul constructeur pour les deux chemins qui
	//l'émettent : l'annonce immédiate et la répétition dans le rapport.
	RTCPPayloadFeedback* CreateReceiverEstimatedMaxBitrateFeedback(DWORD bitrate);
	int SetLocalCryptoSDES(const char* suite, const BYTE* key, const DWORD len);
	int SetRemoteCryptoSDES(const char* suite, const BYTE* key, const DWORD len);
	void SetRTT(DWORD rtt);
	void Stop();
	//Le réacteur effectif : celui qu'on a posé, sinon celui du processus.
	RtpSessionSet* Group();
	int  ReadRTP();
	int  ReadRTCP();
	//Checks ICE de la socket média ; `stun` appartient à l'appelant.
	void ProcessSTUN(STUNMessage* stun,const IPEndpoint& from_addr);
	//Trace agrégée d'un paquet reçu dont le payload type n'est pas négocié (cf.
	//unknownPtCount). Rend toujours 0 : l'appelant jette le paquet.
	int  OnUnknownPayloadType(BYTE type, DWORD ssrc, const IPEndpoint& from);
	//Rattrapage de renégociation : le codec que la map de réception PRÉCÉDENTE
	//donnait à ce payload type, ou RTPMap::NotFound (cf. rtpMapInPrev).
	BYTE CodecFromPreviousMap(BYTE type);
	//Ferme le repli quand ce payload type prouve que le pair a lu notre réponse.
	void RetirePreviousMap(BYTE type);
	//Le payload type sous lequel la map COURANTE porte ce codec, ou
	//RTPMap::NotFound. Sert à la garde du rattrapage et à sa trace.
	BYTE CurrentTypeForCodec(BYTE codec) const;
	void ProcessRTCPPacket(RTCPCompoundPacket *packet, const char * fromAddr);
	void ReSendPacket(int seq);

	int SetRemoteCryptoSDES(const char* suite, const BYTE* key, const DWORD len, int keyRank=0);

	//Un flux vient d'apparaître : réveille les GetPacket qui attendent sa
	//naissance.
	void OnStreamsChanged();

	//--- PollHandler : ce que le réacteur appelle (docs/conception/RTP-REACTOR) ---
	//
	//Privées : personne ne les appelle sur une RTPSession, seul le réacteur les
	//atteint par l'interface. Elles portent ensemble ce que faisait la boucle
	//`poll` de la session : lecture RTP/RTCP, cadence transport-cc, amorçage NAT,
	//checks ICE, handshake DTLS client, tick applicatif, watchdog d'inactivité.
	int  GetPollFds(pollfd* fds, int max) override;
	int  GetNextTimeoutMs(QWORD nowUs) override;
	void OnPollEvents(const pollfd* fds, int count, QWORD nowUs) override;
	void OnPeriodic(QWORD nowUs) override;
	void OnPollError(short revents) override;

	//Conditions des travaux périodiques. Extraites parce qu'elles servent DEUX
	//fois par tour : pour borner l'attente, puis pour décider du travail. Les
	//fonctions Drive* les revérifient elles-mêmes — c'est ce qui rend inoffensif
	//un appel plus fréquent que nécessaire (§2 de la conception).
	bool IsDrivingDTLSClient() const;
	bool IsDrivingICEChecks()  const;
	bool IsNATPriming()        const;
	//La plus proche de deux échéances, -1 valant « aucune ».
	static int Sooner(int waitMs,int candidateMs);

	//Bornes d'attente de la boucle : watchdog, cadence des retransmissions du
	//handshake DTLS client et des checks ICE, rapports transport-cc en attente.
	static const int PollTimeoutMs		= 1000;
	static const int DtlsPollTimeoutMs	= 250;
	static const int TransportFeedbackPollMs = 25;

	//P2 (offreur WebRTC) : pilotage du handshake DTLS en rôle CLIENT
	void FlushDTLS();                    //vide write_bio DTLS vers sendAddr
	void RequestDTLSClientHandshake();   //depuis les setters : réveille le thread Run
	void DriveDTLSClientHandshake();     //depuis Run : émet le ClientHello + retransmet

	//P3 (offreur WebRTC) : binding requests STUN sortants vers un pair ICE-lite
	void SendICEBindingRequest();               //émet un Binding Request vers sendAddr
	void DriveICEChecks();                       //depuis Run : émission + retransmission
	void OnICEConnectivityConfirmed(const IPEndpoint& from); //réponse valide reçue -> débloque

private:
protected:
	

	class RTPStream : public RTPBuffer
	{
	public:
		RTPStream(RTPSession *s,DWORD recSSRC)
		{
			this->s = s;
			recExtSeq = 0;
			this->recSSRC = recSSRC;
			recCycles = 0;
			recTimestamp = 0;
			setZeroTime(&recTimeval);
			
			//Empty stats
			numRecvPackets = 0;
			totalRecvBytes = 0;
			lostRecvPackets = 0;
			totalRecvPacketsSinceLastSR = 0;
			totalRecvBytesSinceLastSR = 0;
			minRecvExtSeqNumSinceLastSR = RTPPacket::MaxExtSeqNum;
			jitter = 0;
			disabled = false;		
		}
		bool	Add(RTPTimedPacket *rtp, DWORD size);
		DWORD 	GetRecSSRC(){return recSSRC;};
		void	SetRecSSRC(DWORD ssrc) {recSSRC = ssrc;}; 
		
		int 				SendReceiverReport();
		RTCPReport* 		CreateReceiverReport();
		
		
		DWORD 	GetNumRecvPackets()					const { return numRecvPackets;	}
		DWORD 	GetTotalRecvBytes()					const { return totalRecvBytes;	}
		DWORD 	GetLostRecvPackets()				const { return lostRecvPackets;	}
		DWORD	GetRecExtSeq() 						const { return recExtSeq;	}	
		DWORD	GetTotalRecvPacketsSinceLastSR() 	const { return totalRecvPacketsSinceLastSR;	}
		DWORD	GetMinRecvExtSeqNumSinceLastSR() 	const { return minRecvExtSeqNumSinceLastSR;	}
		DWORD	GetRecCycles() 						const { return recCycles;	}
                DWORD   GetRecCodec() const { return recCodec; }
	
		bool disabled;
	private:
		DWORD	recExtSeq;
		DWORD	recSSRC;
		DWORD	recTimestamp;
		timeval recTimeval;
		//DWORD	recSR;
		DWORD   recCycles;
		
		//Statistics
		DWORD	numRecvPackets;
		DWORD	totalRecvBytes;
		DWORD	lostRecvPackets;
		
		DWORD	totalRecvPacketsSinceLastSR;
		DWORD	totalRecvBytesSinceLastSR;
		DWORD   minRecvExtSeqNumSinceLastSR;
		DWORD	jitter;

                DWORD   recCodec;
		
		FECDecoder		fec;
		
		RTPSession *s;
	};

	
	
	//Envio y recepcion de rtcp
	int RecvRtcp();
	int SendPacket(RTCPCompoundPacket &rtcp);
	int SendSenderReport();
	int SendFIR(DWORD & ssrc);
	RTCPCompoundPacket* CreateSenderReport();
        /**
	 *  Find the stream associated to the SSRC .
	 */
	RTPStream* getStream(DWORD ssrc);

private:
	typedef std::map<DWORD,RTPTimedPacket*> RTPOrderedPackets;
	typedef std::map<DWORD, RTPStream*> Streams;
protected:
	RemoteRateEstimator*	remoteRateEstimator;
private:
	MediaFrame::Type media;	
	MediaFrame::MediaRole role;
	
	Listener* listener;
	//M-6 : listener géré par shared_ptr (prioritaire si hasWeakListener).
	std::weak_ptr<Listener> weakListener;
	bool hasWeakListener = false;
	//Résout le listener courant : weak_ptr.lock() si armé, sinon wrapper
	//non-possédant du pointeur brut (comportement historique inchangé).
	std::shared_ptr<Listener> LockListener()
	{
		if (hasWeakListener)
			return weakListener.lock();
		return listener ? std::shared_ptr<Listener>(listener, [](Listener*){}) : nullptr;
	}

	Streams streams;
	RTPStream * defaultStream;
	
	bool muxRTCP;
	//Sockets
	int 	simSocket;
	int 	simRtcpSocket;
	int 	simPort;
	int	simRtcpPort;
	bool	inited;
	bool	running;
	//Réacteur posé par le propriétaire de la jambe, NULL = celui du processus.
	RtpSessionSet* pollGroup;

	//Watchdog d'inactivité (gap 5) : horodatage de la dernière activité (paquet reçu
	//OU instant d'armement), seuil d'inactivité en ms (0 = désactivé), drapeau
	//d'armement (le chrono ne court que lorsqu'il est armé — armé au SDP answer) et
	//drapeau anti-rebond (transition unique actif -> inactif).
	timeval	lastRecv;
	DWORD	rtpTimeout;
	bool	rtpTimeoutArmed;
	bool	rtpTimedOut;

	DTLSConnection dtls;
	//P2 : état du handshake DTLS piloté en rôle client (offreur WebRTC).
	//dtlsClientStarted = ClientHello déjà émis (on pilote alors les retransmissions) ;
	//dtlsClientStart   = horodatage de la 1re émission (borne globale) ;
	//dtlsClientFailed  = échec déjà notifié (anti-rebond sur le chemin d'erreur transport).
	bool	dtlsClientStarted;
	timeval	dtlsClientStart;
	bool	dtlsClientFailed;
	//Consommateur des données applicatives DTLS (data channel), et horodatage du
	//dernier battement de sa cadence. ATOMIQUE : le consommateur se retire depuis
	//le thread de contrôle pendant que la boucle le lit, et une lecture déchirée
	//entre « il y a une cadence » et « appelle-le » déréférençait NULL.
	//Contrat de durée de vie : le consommateur doit survivre à la boucle, ou
	//l'arrêter avant de se retirer.
	std::atomic<ApplicationListener*> appListener;
	timeval	lastAppTick;
	bool	encript;
	bool	decript;
	srtp_t	sendSRTPSession;
	BYTE*	sendKey;
	srtp_t	recvSRTPSession;
	srtp_t	recvSRTPSessionRTX;
	srtp_t	recvSRTPSession_secondary;
	srtp_t	recvSRTPSessionRTX_secondary;
	BYTE*	recvKey;

	char*	cname;
	//Nom de la patte, pour les traces uniquement (cf. SetLabel).
	std::wstring label;
	//Agrégation des paquets reçus dont le payload type n'est pas dans rtpMapIn.
	//C'est un état NORMAL et transitoire : entre l'offre et la réception de la
	//réponse, l'offreur émet encore avec son ANCIENNE numérotation (RFC 3264
	//§8), si bien qu'un re-INVITE qui renumérote — Linphone déplace OPUS de 96 à
	//98 — fait tomber quelques centaines de millisecondes de paquets. Une Error
	//par paquet, c'est ~200 lignes par renégociation pour un incident qui se
	//résout tout seul : on trace le premier, puis un résumé compté au plus toutes
	//les `unknownPtPeriod` ms.
	BYTE	unknownPtType;
	DWORD	unknownPtCount;
	timeval	unknownPtLast;
	char*	iceRemoteUsername;
	char*	iceRemotePwd;
	char*	iceLocalUsername;
	char*	iceLocalPwd;
	//Meilleure priorité de candidat distant retenue (trickle ICE Niveau 1, gap 1)
	DWORD	iceRemotePriority;
	//P3 : émission de binding requests STUN sortants (pair ICE-lite qui n'initie
	//jamais). iceConnected = connectivité confirmée (réponse reçue OU check entrant) ;
	//iceCheckStarted = 1re émission faite ; iceCheckRto = intervalle de retransmission
	//courant (backoff) ; iceLastCheck = horodatage de la dernière émission ;
	//iceCheckTransId = transaction id du dernier check émis (corrélation de la réponse).
	bool	iceConnected;
	bool	iceCheckStarted;
	DWORD	iceCheckRto;
	timeval	iceLastCheck;
	BYTE	iceCheckTransId[12];
	//La cible d'envoi courante a été posée par ICE sur une paire VALIDÉE, pas par le
	//plan de contrôle : le `c=` du SDP est alors la moins bonne des deux sources, et
	//SetRemotePort ne doit pas l'écraser. Posé aux deux seuls endroits où ICE écrit
	//sendAddr (réponse valide reçue, check entrant valide) ; effacé par un
	//redémarrage ICE — SetRemoteSTUNCredentials avec un mot de passe DIFFÉRENT —
	//puisque la paire validée ne vaut plus rien pour la nouvelle session.
	bool	iceOwnsSendAddr;
	//Trace « mot de passe ICE local manquant » : une fois, pas un check sur deux.
	bool	iceMissingLocalPwdReported;
	//P5 : anti-rebond one-shot de l'événement « média établi » (premier paquet RTP/SRTP
	//reçu). Remis à false par ArmRTPReceivedNotification() à chaque StartReceiving.
	bool	rtpReceivedNotified;
	//P6 : amorçage NAT symétrique (comedia). natPrimingLeft = paquets restants dans
	//la rafale courante (0 = inactif) ; natPrimingLast = horodatage du dernier envoi,
	//utilisé par le thread Run pour cadencer ~20 ms. Émet un paquet et décrémente.
	int	natPrimingLeft;
	timeval	natPrimingLast;
	int	SendNATPrimingPacket();
	//P7 : rattrapage de la cible d'envoi derrière un NAT symétrique. Le pair annonce
	//une adresse privée dans son SDP mais son RTP nous arrive d'une tout autre
	//adresse:port (mapping NAT) : on ré-aiguille l'envoi vers la source réellement
	//observée. natLatch = correction autorisée — propriété RTP "natLatch", ou 0.0.0.0
	//passé à SetRemotePort ; désactivée par défaut, c'est au plan de contrôle de
	//l'activer ; natCorrected / natRtcpCorrected = correction déjà faite (one-shot :
	//recIP est recalé à chaque paquet de source différente, la cible suivrait sinon
	//le moindre battement). NatCorrectable() porte la règle commune.
	bool	natLatch;
	bool	natCorrected;
	bool	natRtcpCorrected;
	bool	NatCorrectable(const IPAddress& announced);
	std::mutex mutex;

	//--- Prédicats d'adressage (étape 3 du chantier IPv6) ---
	//
	// « Pas encore d'adresse » se disait jusqu'ici `== INADDR_ANY`, répété sur
	// une vingtaine de sites. Or INADDR_ANY est une ADRESSE (0.0.0.0, celle
	// qu'on lie pour écouter partout), pas une sentinelle : la convention ne
	// survit pas au passage en sockaddr_storage, où l'absence se dit
	// `ss_family == AF_UNSPEC`. Ces prédicats rassemblent la convention en UN
	// SEUL endroit — l'étape 5 les réécrit, pas leurs appelants.
	//
	// Ils sont volontairement à comportement CONSTANT : à ce stade, ils rendent
	// exactement ce que rendaient les tests qu'ils remplacent.
	bool HasRemote()     const { return sendAddr.IsSet();     }
	bool HasRemoteRtcp() const { return sendRtcpAddr.IsSet(); }
	bool HasRecIP()      const { return recIP.IsSet();        }
	bool HasIceRemote()  const { return iceRemoteIP.IsSet();  }

	// Deux adresses désignent-elles le même pair ? La question n'est PAS
	// triviale en dual-stack : le même hôte v4 s'écrit `1.2.3.4` quand le
	// contrôleur l'annonce, et `::ffff:1.2.3.4` quand son paquet arrive sur
	// notre socket v6. `IPAddress::operator==` dé-mappe avant de comparer,
	// donc le latching NAT ne se déclenche pas sur cette seule différence
	// d'écriture — c'était LE piège du dual-stack.
	static bool SameAddr(const IPAddress& a, const IPAddress& b) { return a == b; }

	// Destination dans la famille de NOTRE socket : forme v6 mappée quand la
	// socket est dual-stack (le cas par défaut), forme native quand elle est
	// liée à une adresse v4 précise. Un seul endroit décide, donc aucun site
	// d'émission ne peut se tromper de forme.
	IPEndpoint Dest(const IPAddress& addr, WORD port) const
	{
		return (socketFamily == AF_INET6) ? addr.ToDualStack(port) : addr.To(port);
	}

	// Pose la même destination sur les deux jambes. Les deux appelants
	// (StartSending et le basculement sur candidat ICE) le faisaient ligne à
	// ligne ; en oublier une donnerait un RTCP émis vers l'ancien pair.
	void SetRemoteIp(const IPAddress& addr)
	{
		sendAddr     = Dest(addr,sendAddr.Port());
		sendRtcpAddr = Dest(addr,sendRtcpAddr.Port());
	}

	//Famille des sockets média : AF_INET6 + IPV6_V6ONLY=0 par défaut (les deux
	//familles sur une socket), AF_INET si le profil d'adressage impose une
	//adresse de bind v4. Posée par Init.
	int socketFamily;

	//IPV6_V6ONLY=0 sur une socket v6 : elle entend alors les deux familles.
	void SetDualStack(int fd);

	//Adresse à lier, vide = toutes interfaces. C'est elle qui décide de
	//l'interface empruntée quand un profil d'adressage est demandé (voir
	//NETWORK-CONFIGURATION.md) ; vide, on garde l'écoute historique sur `::`.
	IPAddress bindAddress;

	//Profil d'adressage retenu, et le drapeau qui dit qu'il l'a été
	//explicitement — sans quoi on ne pourrait pas distinguer « le contrôleur a
	//demandé publicv4 » de « personne n'a rien demandé ».
	AddressProfiles::Id addressProfile;
	bool                addressProfileSet;

	//Relie les sockets à une autre adresse : arrête la réception, ferme, rouvre,
	//redémarre. Le port local CHANGE — c'est pourquoi cela ne peut se produire
	//qu'avant que le contrôleur n'ait publié le SDP.
	int Rebind(const IPAddress& addr);

	//Tipos
	int 	sendType;

	//Transmision. Destinations RTP et RTCP : deux endpoints DISTINCTS — le
	//rattrapage NAT du RTCP est independant de celui du RTP (voir ReadRTCP).
	IPEndpoint sendAddr;
	IPEndpoint sendRtcpAddr;
	//srtp_protect() chiffre en place ET ajoute le trailer SRTP (jusqu'a
	//SRTP_MAX_TRAILER_LEN octets au-dela de la charge utile) : le tampon doit
	//donc reserver MTU + trailer, sinon debordement a l'emission (le memset du
	//constructeur supposait deja cette taille).
	BYTE 	sendPacket[MTU+SRTP_MAX_TRAILER_LEN];
	WORD    sendSeq;
        DWORD   sendExtSeq;
        DWORD   sendCycles;
	DWORD   sendTime;
	DWORD	sendLastTime;
	DWORD	sendSSRC;
	DWORD	sendSR;
	DWORD	recSR;
	//Recepcion
	BYTE	recBuffer[MTU];
	//Source reellement observee, et pair ICE retenu. Dé-mappees a l'entree :
	//un pair v4 arrivant sur la socket dual-stack se compare a son annonce v4.
	IPAddress recIP;
	IPAddress iceRemoteIP;
	DWORD	  recPort;

	//RTP Map types
	RTPMap* rtpMapIn;
	RTPMap* rtpMapOut;
	//La map de réception PRÉCÉDENTE, gardée quelques secondes après une
	//renégociation (cf. RTP_MAP_FALLBACK_MS), et l'instant où elle a été
	//remplacée. C'est le trou de l'offre/réponse : entre le moment où nous
	//appliquons la nouvelle numérotation et celui où l'offreur reçoit notre
	//réponse, ses paquets portent encore l'ancienne. Les jeter, c'est perdre des
	//centaines de millisecondes de flux — et en vidéo, tout ce qui suit la
	//dernière intra reçue, donc une image figée jusqu'à la suivante.
	RTPMap* rtpMapInPrev;
	timeval	rtpMapInPrevSince;
	//Traces agrégées du rattrapage, mêmes règles que unknownPt* ci-dessus.
	BYTE	salvagedType;
	DWORD	salvagedCount;
	timeval	salvagedLast;
	RTPMap	extMap;

	DWORD	numSendPackets;
	DWORD	totalSendBytes;	
	BYTE	firReqNum;

	DWORD	rtt;
	timeval lastSR;
	timeval lastReceivedSR;
	bool	requestFPU;
	bool	pendingTMBR;
	DWORD	pendingTMBBitrate;

	//FECDecoder		fec;
	bool			useFEC;
	bool			useNACK;
	bool			isNACKEnabled;
	//Le dialecte de feedback négocié, et l'amortisseur qui décide QUAND redire
	//au pair combien il peut envoyer (baisse immédiate, hausse retenue 200 ms).
	BitrateFeedbackMode	bitrateFeedbackMode;
	RembThrottler		bitrateFeedbackThrottler;
	bool			useAbsTime;
	//Extension transport-wide-cc : compteur ecrit sur nos paquets sortants
	//(sender_bwe_plan.md 6.1) ; le pair le renvoie dans ses rapports fmt 15.
	bool			useTransportCC;
	DWORD			transportSeqNum;
	//`extMap` va de l'id du fil vers le type d'extension, comme `rtpMapIn` va du
	//payload type vers le codec : c'est le sens dont la LECTURE a besoin
	//(RTPPacket::ProcessExtensions). L'ECRITURE a besoin de l'inverse, d'ou ces
	//deux ids gardes a part plutot qu'une seconde table a tenir coherente.
	BYTE			absSendTimeExtId;
	BYTE			transportCCExtId;
	//Estimateur émetteur (lot 6.3) : historique alimenté par le thread
	//d'émission, consommé par le thread RTCP — le mutex couvre les deux.
	void ProcessTransportWideFeedback(RTCPRTPFeedback* fb);
	void OnReportedLoss(BYTE fractionLost);
	SentPacketHistory	sentHistory;
	SenderBWE		senderBWE;
	std::mutex		senderBweMutex;
	//Rapports d'arrivee que NOUS devons au pair (lot 4). Ecrit et lu par le
	//seul thread Run (reception RTP puis emission du rapport), donc sans
	//verrou : le sortir de ce thread demanderait d'en poser un.
	int SendTransportWideFeedback(QWORD nowUs);
	TransportWideFeedbackGenerator	transportFeedback;
	bool			transportFeedbackStarted;
	bool 			useOriSeqNum;
	bool 			useOriTS;
	bool 			useExtFIR;
	bool 			useRtcpFIR;

	RTPOrderedPackets	rtxs;
	Use				rtxUse;
	Use				streamUse;
	//Naissance d'un flux : `GetPacket` attend cet événement au lieu de sonder
	//quand le SSRC demandé n'a pas encore de flux — le premier paquet reçu le
	//crée. Le compteur est lu AVANT la recherche, donc un flux né entre les deux
	//est vu par le prédicat de l'attente : aucun réveil n'est perdu.
	::Wait				streamWait;
	std::atomic<DWORD>		streamGeneration{0};
    bool        	resetRequested;
	
	DWORD			lastSendSSRC;

};

#endif
