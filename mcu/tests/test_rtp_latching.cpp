/**
 * test_rtp_latching.cpp — latching RTP symétrique (NAT « comedia ») de RTPSession.
 *
 * Le pair annonce une adresse dans son SDP, mais son média nous arrive parfois
 * d'ailleurs : un NAT symétrique a réécrit l'adresse ET le port. Émettre vers
 * l'annonce ne mènerait alors nulle part. `RTPSession` sait donc ré-aiguiller sa
 * cible d'envoi sur la source réellement observée — mais seulement quand c'est
 * légitime, sinon la cible battrait au gré du moindre paquet égaré. La politique
 * (cf. `RTPSession::NatCorrectable`) est :
 *
 *   - le plan de contrôle doit l'AUTORISER : propriété RTP `natLatch`, ou une
 *     annonce `0.0.0.0` (le contrôleur déclare ignorer l'adresse du pair) ;
 *   - l'adresse annoncée doit être PRIVÉE (RFC 1918, CGNAT RFC 6598, link-local) :
 *     sur une adresse publique, une divergence est plus probablement du routage
 *     asymétrique légitime qu'un NAT à corriger ;
 *   - ICE ne doit pas être en jeu : quand le PAIR a répondu ses credentials STUN,
 *     ce sont les checks de connectivité qui posent la cible. Nos seuls
 *     credentials ne comptent pas — offrir ICE n'est pas le pratiquer ;
 *   - la correction est ONE-SHOT par cible (`natCorrected`), et le droit est
 *     rouvert par un nouveau `SetRemotePort` (re-INVITE / UPDATE).
 *
 * CES TESTS N'INSPECTENT AUCUN ÉTAT INTERNE. `recIP`, `natCorrected` et
 * `NatCorrectable` sont privés, et l'idiome du pointeur sur membre utilisé pour
 * `Mosaic::BuildDesc` (protégé) ne s'applique pas au privé. On teste donc ce qui
 * compte vraiment et ce que voit le pair : **où les paquets atterrissent**. Un
 * socket sonde en loopback joue le pair NATé — il émet le média, puis vérifie s'il
 * reçoit celui de la session.
 *
 * Adresses de test : `SetRemotePort` déclenche immédiatement une rafale
 * d'amorçage NAT vers l'adresse annoncée (`ArmNATPriming`). Toutes les adresses
 * utilisées ici sont donc choisies non routées (192.168.255.254 côté privé,
 * 240.0.0.1 — classe E réservée — côté « public ») pour ne jamais arroser une
 * machine réelle du réseau.
 */
#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "log.h"
#include "medkit/stunmessage.h"
#include "rtp.h"
#include "rtpsession.h"

namespace {

// Adresse privée (RFC 1918) annoncée par un pair NATé : ouvre droit au
// rattrapage. Choisie hors de tout plan d'adressage plausible.
const char* const kAnnouncedPrivate = "192.168.255.254";
// Adresse « publique » du point de vue de la politique (donc PAS de rattrapage).
// 240.0.0.1 : classe E réservée, jamais routée.
const char* const kAnnouncedPublic  = "240.0.0.1";

// Charge utile reconnaissable : distingue le média émis par SendPacket des
// paquets d'amorçage NAT (12 octets d'en-tête nu, sans charge utile).
const BYTE kMagic[] = { 0xC0, 0xFF, 0xEE, 0x42, 0x13, 0x37 };

// Listener minimal : la session en exige un non nul.
class StubListener : public RTPSession::Listener
{
public:
	void onFPURequested(RTPSession*) override { fpuRequests++; }
	void onReceiverEstimatedMaxBitrate(RTPSession*, DWORD) override {}
	void onTempMaxMediaStreamBitrateRequest(RTPSession*, DWORD, DWORD) override {}

	int fpuRequests = 0;
};

// Socket UDP en loopback jouant le pair distant.
class ProbeSocket
{
public:
	// `ip` permet de simuler un pair qui change d'ADRESSE et non seulement de port :
	// tout 127.0.0.0/8 est local sous Linux, donc 127.0.0.2 fait un second pair
	// distinct aux yeux de la session.
	bool Open(const char* ip = "127.0.0.1")
	{
		fd = socket(PF_INET, SOCK_DGRAM, 0);
		if (fd < 0)
			return false;

		sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family      = AF_INET;
		addr.sin_addr.s_addr = inet_addr(ip);
		addr.sin_port        = 0;   // port éphémère
		if (bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0)
			return false;

		socklen_t len = sizeof(bound);
		return getsockname(fd, (sockaddr*)&bound, &len) == 0;
	}

	~ProbeSocket() { if (fd >= 0) close(fd); }

	// Émet un RTP minimal mais valide vers la session (V=2, 12 octets d'en-tête).
	// C'est la SOURCE observée par la session : le latch se produit sur ce paquet.
	bool SendRtpTo(int port, BYTE payloadType = 0, DWORD ssrc = 0x1234ABCD)
	{
		BYTE packet[12];
		memset(packet, 0, sizeof(packet));
		packet[0] = 0x80;                       // version 2
		packet[1] = (BYTE)(payloadType & 0x7f);
		packet[2] = 0; packet[3] = 1;           // seq
		packet[8]  = (BYTE)(ssrc >> 24); packet[9]  = (BYTE)(ssrc >> 16);
		packet[10] = (BYTE)(ssrc >> 8);  packet[11] = (BYTE)ssrc;

		sockaddr_in to;
		memset(&to, 0, sizeof(to));
		to.sin_family      = AF_INET;
		to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		to.sin_port        = htons(port);
		return sendto(fd, packet, sizeof(packet), 0, (sockaddr*)&to, sizeof(to)) == (ssize_t)sizeof(packet);
	}

	// Émet un STUN Binding Request, ce que fait un pair ICE quand il vérifie la
	// connectivité. C'est ce check qui fait poser la cible d'envoi par ICE sur la
	// source observée — un chemin distinct du rattrapage NAT, et le seul que suit
	// un navigateur. `UseCandidate` désigne la paire, comme dans un vrai contrôle.
	bool SendStunBindingTo(int port)
	{
		BYTE transId[12];
		memset(transId, 0, sizeof(transId));
		transId[0] = 0x2a;

		STUNMessage request(STUNMessage::Request, STUNMessage::Binding, transId);
		request.AddAttribute(STUNMessage::Attribute::UseCandidate);

		BYTE buffer[MTU];
		const DWORD len = request.NonAuthenticatedFingerPrint(buffer, sizeof(buffer));
		if (!len)
			return false;

		sockaddr_in to;
		memset(&to, 0, sizeof(to));
		to.sin_family      = AF_INET;
		to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		to.sin_port        = htons(port);
		return sendto(fd, buffer, len, 0, (sockaddr*)&to, sizeof(to)) == (ssize_t)len;
	}

	// Attend un datagramme portant kMagic (donc issu de SendPacket, pas de
	// l'amorçage NAT). Retourne false au bout de timeoutMs sans rien de tel.
	bool WaitForMagic(int timeoutMs)
	{
		pollfd pfd = { fd, POLLIN, 0 };
		while (timeoutMs > 0)
		{
			const int slice = timeoutMs < 50 ? timeoutMs : 50;
			const int ret = poll(&pfd, 1, slice);
			timeoutMs -= slice;
			if (ret <= 0)
				continue;

			BYTE buffer[MTU];
			const ssize_t size = recv(fd, buffer, sizeof(buffer), 0);
			if (size < (ssize_t)(12 + sizeof(kMagic)))
				continue;   // en-tête nu : paquet d'amorçage, on l'ignore
			if (memcmp(buffer + 12, kMagic, sizeof(kMagic)) == 0)
				return true;
		}
		return false;
	}

	// Laisse la session traiter ce qu'on vient de lui envoyer, sans rien émettre :
	// le thread Run relève la source observée en quelques microsecondes. Jette ce
	// qui arriverait entre-temps.
	void Drain(int ms)
	{
		pollfd pfd = { fd, POLLIN, 0 };
		while (ms > 0)
		{
			const int slice = ms < 25 ? ms : 25;
			ms -= slice;
			if (poll(&pfd, 1, slice) > 0)
			{
				BYTE buffer[MTU];
				recv(fd, buffer, sizeof(buffer), 0);
			}
		}
	}

	int Port() const { return ntohs(bound.sin_port); }

	// Le pair JETTE une réponse sans MESSAGE-INTEGRITY (RFC 5389 §10.1.2) : c'est
	// cette distinction qui décide si la paire sera validée.
	enum class StunAnswer { None, Authenticated, Unauthenticated };

	StunAnswer WaitForStunResponse(int timeoutMs)
	{
		pollfd pfd = { fd, POLLIN, 0 };
		while (timeoutMs > 0)
		{
			const int slice = timeoutMs < 50 ? timeoutMs : 50;
			timeoutMs -= slice;
			if (poll(&pfd, 1, slice) <= 0)
				continue;

			BYTE buffer[MTU];
			const ssize_t size = recv(fd, buffer, sizeof(buffer), 0);
			if (size <= 0)
				continue;

			// Le RTP d'amorçage NAT et le Binding REQUEST en retour arrivent ici aussi.
			STUNMessage* msg = STUNMessage::Parse(buffer, (DWORD)size);
			if (!msg)
				continue;

			const bool isResponse = msg->GetType() == STUNMessage::Response;
			const bool integrity  = msg->HasAttribute(STUNMessage::Attribute::MessageIntegrity);
			delete msg;

			if (isResponse)
				return integrity ? StunAnswer::Authenticated : StunAnswer::Unauthenticated;
		}
		return StunAnswer::None;
	}

private:
	int         fd = -1;
	sockaddr_in bound {};
};

// Session prête à émettre : ports bindés, thread de réception démarré (Init),
// table d'émission renseignée (SendPacket déréférence rtpMapOut sans garde).
class Session
{
public:
	explicit Session(bool natLatchProperty = false)
		: session(MediaFrame::Audio, &listener)
	{
		ok = (session.Init() == 1);
		if (!ok)
			return;

		RTPMap map;
		map[kPayloadType] = kCodec;      // type -> codec
		session.SetSendingRTPMap(map);
		session.SetReceivingRTPMap(map);

		if (natLatchProperty)
		{
			Properties props;
			props["natLatch"] = "1";
			session.SetProperties(props);
		}
	}

	~Session() { if (ok) session.End(); }

	// Émet un paquet média porteur de kMagic vers la cible courante.
	void SendMagic()
	{
		// PIÈGE connu : RTPPacket(media, codec, type) avec des littéraux 0 est
		// ambigu (concurrence avec le ctor (media, BYTE*, DWORD)). Passer des DWORD.
		DWORD codec = kCodec;
		DWORD type  = kPayloadType;
		RTPPacket packet(MediaFrame::Audio, codec, type);
		memcpy(packet.GetMediaData(), kMagic, sizeof(kMagic));
		packet.SetMediaLength(sizeof(kMagic));
		session.SendPacket(packet);
	}

	// Le latch a lieu dans le thread Run, à l'arrivée du paquet de la sonde : on
	// ré-émet donc périodiquement jusqu'à ce que la sonde reçoive, ou expiration.
	bool ReachesProbeWithin(ProbeSocket& probe, int timeoutMs)
	{
		for (int waited = 0; waited < timeoutMs; waited += 100)
		{
			SendMagic();
			if (probe.WaitForMagic(100))
				return true;
		}
		return false;
	}

	bool          ok = false;
	StubListener  listener;
	RTPSession    session;

	// constexpr : implicitement inline en C++17, donc pas de définition hors classe
	// à fournir malgré l'usage par référence (RTPMap::operator[]).
	static constexpr BYTE kPayloadType = 0;   // PCMU
	static constexpr BYTE kCodec       = 0;   // AudioCodec::PCMU
};

// Délais : généreux pour ne pas être flaky sous charge, mais bornés. Le cas
// négatif doit attendre assez longtemps pour être crédible.
const int kExpectTimeoutMs = 2000;
const int kDenyTimeoutMs   = 600;

// Loopback UDP indisponible (sandbox réseau) : test SKIPPÉ plutôt qu'échoué,
// comme WebSocketEcho.
#define REQUIRE_LOOPBACK(probe, sess)                                          \
	do {                                                                   \
		if (!(probe).Open())                                           \
			GTEST_SKIP() << "socket loopback indisponible";        \
		if (!(sess).ok)                                                \
			GTEST_SKIP() << "impossible de binder une paire de ports RTP"; \
	} while (0)

} // namespace

// Le contrôleur annonce 0.0.0.0 : il déclare ne pas connaître l'adresse du pair
// et s'en remet à la source observée. La session doit donc émettre vers la sonde.
TEST(RtpLatching, LatchesOnObservedSourceWhenAnnouncedAddressIsUnknown)
{
	ProbeSocket probe;
	Session sess;
	REQUIRE_LOOPBACK(probe, sess);

	char any[] = "0.0.0.0";
	sess.session.SetRemotePort(any, 0);

	ASSERT_TRUE(probe.SendRtpTo(sess.session.GetLocalPort()));
	EXPECT_TRUE(sess.ReachesProbeWithin(probe, kExpectTimeoutMs))
		<< "annonce 0.0.0.0 : le media doit suivre la source observee";
}

// Cas du NAT symétrique : annonce privée + autorisation du contrôleur. Le média
// doit être ré-aiguillé de l'annonce (injoignable) vers la source réelle.
TEST(RtpLatching, ReAimsFromPrivateAnnouncementToObservedSource)
{
	ProbeSocket probe;
	Session sess(/*natLatchProperty=*/true);
	REQUIRE_LOOPBACK(probe, sess);

	char announced[] = "192.168.255.254";
	sess.session.SetRemotePort(announced, 5000);

	ASSERT_TRUE(probe.SendRtpTo(sess.session.GetLocalPort()));
	EXPECT_TRUE(sess.ReachesProbeWithin(probe, kExpectTimeoutMs))
		<< "annonce privee + natLatch : le media doit suivre la source observee";
}

// Sans autorisation du plan de contrôle, aucune correction : le média continue
// vers l'annonce, même si la source observée diffère. C'est le comportement par
// défaut, et il est délibéré.
TEST(RtpLatching, DoesNotReAimWhenLatchingIsNotAuthorised)
{
	ProbeSocket probe;
	Session sess(/*natLatchProperty=*/false);
	REQUIRE_LOOPBACK(probe, sess);

	char announced[] = "192.168.255.254";
	sess.session.SetRemotePort(announced, 5000);

	ASSERT_TRUE(probe.SendRtpTo(sess.session.GetLocalPort()));
	EXPECT_FALSE(sess.ReachesProbeWithin(probe, kDenyTimeoutMs))
		<< "sans natLatch, la cible d'envoi ne doit pas bouger";
}

// Annonce PUBLIQUE : même autorisé, pas de rattrapage. Une divergence sur une
// adresse publique est plus probablement du routage asymétrique légitime.
TEST(RtpLatching, DoesNotReAimFromAPublicAnnouncement)
{
	ProbeSocket probe;
	Session sess(/*natLatchProperty=*/true);
	REQUIRE_LOOPBACK(probe, sess);

	char announced[] = "240.0.0.1";
	sess.session.SetRemotePort(announced, 5000);

	ASSERT_TRUE(probe.SendRtpTo(sess.session.GetLocalPort()));
	EXPECT_FALSE(sess.ReachesProbeWithin(probe, kDenyTimeoutMs))
		<< "annonce publique : pas de rattrapage (routage asymetrique legitime)";
}

// ADVERSE — le piège du vocabulaire, appliqué à la POLITIQUE. 192.0.2.1 est une
// adresse de DOCUMENTATION (RFC 5737) : `IPAddress::IsPrivate()` la dit non
// routable sur l'Internet public, et pourtant elle n'est nullement NATée. Si
// `NatCorrectable` consultait `IsPrivate()` plutôt que `IsPrivateV4()`, le
// rattrapage s'ouvrirait ici — sur une adresse qui n'en relève pas.
TEST(RtpLatching, DoesNotReAimFromANonRoutableButNonPrivateAnnouncement)
{
	ProbeSocket probe;
	Session sess(/*natLatchProperty=*/true);
	REQUIRE_LOOPBACK(probe, sess);

	char announced[] = "192.0.2.1";
	sess.session.SetRemotePort(announced, 5000);

	ASSERT_TRUE(probe.SendRtpTo(sess.session.GetLocalPort()));
	EXPECT_FALSE(sess.ReachesProbeWithin(probe, kDenyTimeoutMs))
		<< "non routable n'est pas privee : aucun rattrapage NAT ici";
}

// ICE réellement en jeu : le pair a répondu avec ses credentials, donc les checks
// de connectivité désigneront la cible. Le rattrapage se retire.
TEST(RtpLatching, DoesNotReAimWhenIceIsInPlace)
{
	ProbeSocket probe;
	Session sess(/*natLatchProperty=*/true);
	REQUIRE_LOOPBACK(probe, sess);

	sess.session.SetLocalSTUNCredentials("localufrag", "localpwd");
	sess.session.SetRemoteSTUNCredentials("remoteufrag", "remotepwd");

	char announced[] = "192.168.255.254";
	sess.session.SetRemotePort(announced, 5000);

	ASSERT_TRUE(probe.SendRtpTo(sess.session.GetLocalPort()));
	EXPECT_FALSE(sess.ReachesProbeWithin(probe, kDenyTimeoutMs))
		<< "ICE pose la cible lui-meme : ne pas la lui disputer";
}

// ADVERSE — offrir ICE n'est pas le pratiquer. Nous avons annoncé nos credentials,
// le pair a répondu SANS ICE (un Linphone, un poste SIP quelconque) : personne ne
// posera jamais la cible, puisque les checks entrants sont jetés faute
// d'iceRemotePwd. Vetoer sur nos SEULS credentials laissait la jambe muette pour
// tout l'appel — trafic du 2026-08-21, Alice WebRTC vers Bob Linphone : la poignée
// DTLS aboutissait, et Bob ne recevait pas un paquet RTP de bout en bout.
TEST(RtpLatching, ReAimsWhenWeOfferedIceAndThePeerDeclinedIt)
{
	ProbeSocket probe;
	Session sess(/*natLatchProperty=*/true);
	REQUIRE_LOOPBACK(probe, sess);

	sess.session.SetLocalSTUNCredentials("localufrag", "localpwd");

	char announced[] = "192.168.255.254";
	sess.session.SetRemotePort(announced, 5000);

	ASSERT_TRUE(probe.SendRtpTo(sess.session.GetLocalPort()));
	EXPECT_TRUE(sess.ReachesProbeWithin(probe, kExpectTimeoutMs))
		<< "ICE offert et decline : le rattrapage est la seule chose qui reste";
}

// Un nouveau SetRemotePort (re-INVITE, UPDATE) rouvre le droit au rattrapage :
// sinon un pair qui change de mapping resterait coincé sur l'ancienne cible.
// Ici le pair déplacé se manifeste AVANT toute nouvelle émission — le mécanisme
// de réouverture fonctionne alors comme prévu (cas d'un émetteur au repos :
// participant muet, média en pause).
TEST(RtpLatching, NewRemotePortReopensTheRightToReAim)
{
	ProbeSocket first;
	Session sess(/*natLatchProperty=*/true);
	REQUIRE_LOOPBACK(first, sess);

	char announced[] = "192.168.255.254";
	sess.session.SetRemotePort(announced, 5000);

	ASSERT_TRUE(first.SendRtpTo(sess.session.GetLocalPort()));
	ASSERT_TRUE(sess.ReachesProbeWithin(first, kExpectTimeoutMs));

	// Le plan de contrôle repose une cible : la correction précédente est caduque.
	char reannounced[] = "192.168.255.253";
	sess.session.SetRemotePort(reannounced, 5002);

	// Le pair déplacé se manifeste depuis une AUTRE adresse (un changement de
	// port seul ne serait pas relevé, cf. PortOnlyMappingChangeIsNotFollowed),
	// et AVANT que nous n'ayons ré-émis quoi que ce soit.
	ProbeSocket second;
	if (!second.Open("127.0.0.2"))
		GTEST_SKIP() << "adresse loopback secondaire indisponible";
	ASSERT_TRUE(second.SendRtpTo(sess.session.GetLocalPort()));
	second.Drain(150);   // laisse la session relever la nouvelle source

	EXPECT_TRUE(sess.ReachesProbeWithin(second, kExpectTimeoutMs))
		<< "apres un nouveau SetRemotePort, le rattrapage doit pouvoir rejouer";
}

// CARACTÉRISATION D'UN DÉFAUT (pas d'un choix de conception) : ce test décrit le
// comportement actuel, qui CONTREDIT l'intention écrite dans `SetRemotePort`
// (« On rouvre le droit au rattrapage, sinon un pair qui change de mapping
// resterait coincé sur l'ancien »).
//
// `SetRemotePort` remet `natCorrected` à false mais laisse l'OBSERVATION
// précédente (`recIP`/`recPort`) en place. Or le média coule en continu : le
// premier paquet sortant après le re-INVITE arrive avant que le pair déplacé ne
// se soit manifesté, et `SendPacket` ré-aiguille alors sur l'observation
// PÉRIMÉE — ce qui reconsomme le one-shot. Quand le nouveau pair se manifeste
// enfin, `natCorrected` vaut déjà true : plus aucune correction n'est possible
// et la session reste bloquée sur l'ancien pair, en journalisant en boucle
// « WARNING Trying to send packet from different ip address than receiving one ».
//
// En production la course est quasiment toujours perdue (audio émis toutes les
// 20 ms contre un pair qui ne parle qu'après son answer).
//
// Ce test RÉUSSIT tant que le défaut existe. Correction probable : oublier
// l'observation (`recIP`/`recPort`) quand le plan de contrôle pose une nouvelle
// cible. Il faudra alors inverser les deux attentes ci-dessous.
TEST(RtpLatching, StaleObservationBurnsTheOneShotWhenSendingContinues)
{
	ProbeSocket first;
	Session sess(/*natLatchProperty=*/true);
	REQUIRE_LOOPBACK(first, sess);

	char announced[] = "192.168.255.254";
	sess.session.SetRemotePort(announced, 5000);

	ASSERT_TRUE(first.SendRtpTo(sess.session.GetLocalPort()));
	ASSERT_TRUE(sess.ReachesProbeWithin(first, kExpectTimeoutMs));

	char reannounced[] = "192.168.255.253";
	sess.session.SetRemotePort(reannounced, 5002);

	// L'émission continue AVANT que le pair déplacé ne se manifeste : le
	// rattrapage se rejoue sur l'ancienne source et brûle le one-shot.
	sess.SendMagic();

	ProbeSocket second;
	if (!second.Open("127.0.0.2"))
		GTEST_SKIP() << "adresse loopback secondaire indisponible";
	ASSERT_TRUE(second.SendRtpTo(sess.session.GetLocalPort()));
	second.Drain(150);

	EXPECT_FALSE(sess.ReachesProbeWithin(second, kDenyTimeoutMs))
		<< "defaut caracterise : le nouveau pair n'est jamais suivi";
	EXPECT_TRUE(sess.ReachesProbeWithin(first, kExpectTimeoutMs))
		<< "le media reste bloque sur l'ancien pair";
}

// CARACTÉRISATION d'une limite du latching, sur le modèle d'`Amf.NumberZeroDecodeQuirk`.
//
// La source observée n'est relevée que lorsque l'ADRESSE change
// (`rtpsession.cpp:2097` compare `recIP` seul) : `recPort` n'est donc jamais
// rafraîchi quand le pair garde son IP et change de PORT — ce qu'un NAT
// symétrique fait pourtant couramment en rebindant son mapping. Le média
// continue alors vers l'ancien port. Le commentaire de `SendPacket`
// (« recIP est recalé sur *chaque* paquet de source différente ») décrit
// l'intention, pas le code.
//
// Ce test RÉUSSIT tant que la limite existe. S'il se met à échouer, c'est
// qu'elle a été corrigée : remplacer alors les deux attentes (le média doit
// suivre le nouveau port, donc `second` reçoit et `first` non).
TEST(RtpLatching, PortOnlyMappingChangeIsNotFollowed)
{
	ProbeSocket first;
	Session sess(/*natLatchProperty=*/true);
	REQUIRE_LOOPBACK(first, sess);

	char announced[] = "192.168.255.254";
	sess.session.SetRemotePort(announced, 5000);

	ASSERT_TRUE(first.SendRtpTo(sess.session.GetLocalPort()));
	ASSERT_TRUE(sess.ReachesProbeWithin(first, kExpectTimeoutMs));

	// Même adresse, port source différent : le nouveau mapping du NAT.
	ProbeSocket second;
	if (!second.Open())
		GTEST_SKIP() << "socket loopback indisponible";
	ASSERT_TRUE(second.SendRtpTo(sess.session.GetLocalPort()));

	EXPECT_FALSE(sess.ReachesProbeWithin(second, kDenyTimeoutMs))
		<< "limite caracterisee : le changement de port seul n'est pas suivi";
	EXPECT_TRUE(sess.ReachesProbeWithin(first, kExpectTimeoutMs))
		<< "le media reste dirige vers l'ancien port";
}

// La réception d'un paquet d'une source inattendue doit demander une image clé
// (le décodeur d'en face repart de zéro après un changement de chemin).
TEST(RtpLatching, RequestsAKeyFrameOnSourceChange)
{
	ProbeSocket probe;
	Session sess(/*natLatchProperty=*/true);
	REQUIRE_LOOPBACK(probe, sess);

	char announced[] = "192.168.255.254";
	sess.session.SetRemotePort(announced, 5000);

	ASSERT_TRUE(probe.SendRtpTo(sess.session.GetLocalPort()));
	ASSERT_TRUE(sess.ReachesProbeWithin(probe, kExpectTimeoutMs));
	EXPECT_GT(sess.listener.fpuRequests, 0)
		<< "un changement de source observee doit declencher onFPURequested";
}

// ─── La cible posée par ICE, face au plan de contrôle ────────────────────────
//
// Sur une jambe ICE, la cible d'envoi n'est PAS celle du `c=` : elle est celle de
// la paire que les checks de connectivité ont validée. `StartSending` est pourtant
// rappelé à chaque renégociation avec le `c=` d'origine, sans que rien n'ait bougé
// côté réseau — et il écrasait la paire validée. Le pair ne remontrait sa vraie
// adresse qu'au check STUN suivant : trou de média à CHAQUE renégociation, 0,9 s
// mesurée sur la jambe WebRTC du trafic du 2026-08-21, audio et vidéo ensemble
// (l'appelée allume sa caméra, l'appelant devient sourd et aveugle le temps que
// le prochain check passe).
//
// `natLatch` reste à false dans les deux tests : le rattrapage NAT est un tout
// autre chemin, et le laisser éteint prouve que c'est bien ICE qui pose la cible.

TEST(RtpLatching, TheIceValidatedTargetSurvivesARenegotiation)
{
	ProbeSocket probe;
	Session sess;
	REQUIRE_LOOPBACK(probe, sess);

	sess.session.SetLocalSTUNCredentials("localufrag", "localpwd");
	sess.session.SetRemoteSTUNCredentials("remoteufrag", "remotepwd");

	char announced[] = "192.168.255.254";
	sess.session.SetRemotePort(announced, 5000);

	// Le pair se manifeste par un check : ICE valide la paire et pose la cible.
	ASSERT_TRUE(probe.SendStunBindingTo(sess.session.GetLocalPort()));
	ASSERT_TRUE(sess.ReachesProbeWithin(probe, kExpectTimeoutMs))
		<< "prealable : un check entrant doit poser la cible sur la source observee";

	// Renégociation : le contrôleur repose le `c=` d'origine, à l'identique.
	sess.session.SetRemotePort(announced, 5000);
	probe.Drain(150);   // sinon un kMagic de la phase precedente ferait passer la suite

	EXPECT_TRUE(sess.ReachesProbeWithin(probe, kExpectTimeoutMs))
		<< "la cible validee par ICE doit survivre a la renegociation";
}

// Le pendant, sans lequel le correctif enfermerait la session sur un pair parti :
// un pair qui se déplace vraiment redémarre ICE (RFC 8445 §9), c'est-à-dire annonce
// un NOUVEAU mot de passe. La paire validée est alors périmée et la cible revient
// au plan de contrôle.
TEST(RtpLatching, AnIceRestartGivesTheTargetBackToTheControlPlane)
{
	ProbeSocket probe;
	Session sess;
	REQUIRE_LOOPBACK(probe, sess);

	sess.session.SetLocalSTUNCredentials("localufrag", "localpwd");
	sess.session.SetRemoteSTUNCredentials("remoteufrag", "remotepwd");

	char announced[] = "192.168.255.254";
	sess.session.SetRemotePort(announced, 5000);

	ASSERT_TRUE(probe.SendStunBindingTo(sess.session.GetLocalPort()));
	ASSERT_TRUE(sess.ReachesProbeWithin(probe, kExpectTimeoutMs))
		<< "prealable : un check entrant doit poser la cible sur la source observee";

	// Redémarrage ICE, puis la nouvelle adresse du plan de contrôle.
	sess.session.SetRemoteSTUNCredentials("remoteufrag2", "remotepwd2");
	sess.session.SetRemotePort(announced, 5000);
	probe.Drain(150);

	EXPECT_FALSE(sess.ReachesProbeWithin(probe, kDenyTimeoutMs))
		<< "apres un redemarrage ICE, la cible redevient celle du plan de controle";
}

// AUTHENTIFICATION DES CHECKS ICE ENTRANTS
//
// Une jambe ICE a besoin des DEUX mots de passe : celui du pair pour signer nos
// checks, le nôtre pour signer nos réponses. Jambe data channel du 2026-09-07 :
// le contrôleur avait poussé le distant et oublié le nôtre. Réponses non signées,
// jetées par le navigateur, paire jamais validée, aucun ClientHello DTLS, pas
// d'association SCTP. 15 s de checks, puis abandon. Seule trace : une ligne DBG.

// Cas nominal : les deux mots de passe, la réponse est signée.
TEST(RtpIceAuthentication, AnswersAnIncomingCheckWithMessageIntegrity)
{
	ProbeSocket probe;
	Session sess;
	REQUIRE_LOOPBACK(probe, sess);

	sess.session.SetLocalSTUNCredentials("localufrag", "localpwd");
	sess.session.SetRemoteSTUNCredentials("remoteufrag", "remotepwd");

	ASSERT_TRUE(probe.SendStunBindingTo(sess.session.GetLocalPort()));
	EXPECT_EQ(ProbeSocket::StunAnswer::Authenticated,
	          probe.WaitForStunResponse(kExpectTimeoutMs))
		<< "une Binding Response sans MESSAGE-INTEGRITY est jetee par le pair";
}

// Le serveur ne peut pas fabriquer le mot de passe manquant, mais il sait qu'il
// manque : il doit NOMMER l'appel oublié. C'est cette trace qui manquait.
TEST(RtpIceAuthentication, ShoutsWhenTheControllerForgotOurLocalIcePassword)
{
	ProbeSocket probe;
	Session sess;
	REQUIRE_LOOPBACK(probe, sess);

	sess.session.SetRemoteSTUNCredentials("remoteufrag", "remotepwd");

	::testing::internal::CaptureStdout();
	ASSERT_TRUE(probe.SendStunBindingTo(sess.session.GetLocalPort()));
	probe.Drain(300);            // laisse le thread Run traiter le check
	fflush(stdout);              // la trace est ecrite par un AUTRE thread
	const std::string traces = ::testing::internal::GetCapturedStdout();

	EXPECT_NE(std::string::npos, traces.find("SetLocalSTUNCredentials"))
		<< "le serveur doit nommer l'appel manquant, pas se taire. Trace :\n" << traces;
}

// Un pair réémet toutes les 100 ms : une ligne par check en ferait 128 par appel.
TEST(RtpIceAuthentication, SaysItOnceAndNotOncePerCheck)
{
	ProbeSocket probe;
	Session sess;
	REQUIRE_LOOPBACK(probe, sess);

	sess.session.SetRemoteSTUNCredentials("remoteufrag", "remotepwd");

	::testing::internal::CaptureStdout();
	for (int i = 0; i < 5; i++)
	{
		ASSERT_TRUE(probe.SendStunBindingTo(sess.session.GetLocalPort()));
		probe.Drain(100);
	}
	fflush(stdout);
	const std::string traces = ::testing::internal::GetCapturedStdout();

	int occurrences = 0;
	for (size_t at = traces.find("SetLocalSTUNCredentials");
	     at != std::string::npos;
	     at = traces.find("SetLocalSTUNCredentials", at + 1))
		occurrences++;

	EXPECT_EQ(1, occurrences)
		<< "une seule fois par jambe, pas une par check. Trace :\n" << traces;
}
