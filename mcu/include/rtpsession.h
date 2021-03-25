#ifndef _RTPSESSION_H_
#define _RTPSESSION_H_
#include <sys/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <mutex>
#include <map>
#include <string>
#include <poll.h>
#include <srtp/srtp.h>
#include "config.h"
#include "use.h"
#include "rtp.h"
#include "rtpbuffer.h"
#include "remoteratecontrol.h"
#include "fecdecoder.h"
#include "stunmessage.h"
#include "remoterateestimator.h"
#include "dtls.h"



class RTPSession : 
	public RemoteRateEstimator::Listener,
	public DTLSConnection::Listener
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
	};
	
public:
	
public:
	static bool SetPortRange(int minPort, int maxPort);
	static DWORD GetMinPort() { return minLocalPort; }
	static DWORD GetMaxPort() { return maxLocalPort; }

private:
	// Admissible port range
	static DWORD minLocalPort;
	static DWORD maxLocalPort;
	
public:
	RTPSession(MediaFrame::Type media,Listener *listener, MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	~RTPSession();
	int Init();
	void SetRemoteRateEstimator(RemoteRateEstimator* estimator);
	int SetLocalPort(int recvPort);
	int GetLocalPort();
	int SetRemotePort(char *ip,int sendPort);
	int 					GetRemotePort();
	
	
	RemoteRateEstimator* 	GetRemoteRateEstimator() 	{	return remoteRateEstimator; };
	bool 			SendBitrateFeedback() 		{	return sendBitrateFeedback; };
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

	int ForwardPacket( RTPPacket &packet, DWORD recssrc );
	
	int SendEmptyPacket();
	int SendPacket(RTPPacket &packet,DWORD timestamp);
	int SendPacket(RTPPacket &packet);
	
	
	void CancelGetPacket();
	
	// Multi stream
	RTPPacket* GetPacket();
	RTPPacket* GetPacket(DWORD & ssrc);
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

	int SetLocalCryptoSDES(const char* suite, const char* key64);
	int SetRemoteCryptoSDES(const char* suite, const char* key64,int keyRank=0);
	int SetRemoteCryptoDTLS(const char *setup,const char *hash,const char *fingerprint);
	int SetLocalSTUNCredentials(const char* username, const char* pwd);
	int SetRemoteSTUNCredentials(const char* username, const char* pwd);
	int SetProperties(const Properties& properties);
	int RequestFPU();
	int RequestFPU(DWORD & ssrc);
	
	int SendTempMaxMediaStreamBitrateNotification(DWORD bitrate,DWORD overhead);

	virtual void onTargetBitrateRequested(DWORD bitrate);
	virtual void onDTLSSetup(DTLSConnection::Suite suite,BYTE* localMasterKey,DWORD localMasterKeySize,BYTE* remoteMasterKey,DWORD remoteMasterKeySize);
private:
	int SetLocalCryptoSDES(const char* suite, const BYTE* key, const DWORD len);
	int SetRemoteCryptoSDES(const char* suite, const BYTE* key, const DWORD len);
	void SetRTT(DWORD rtt);
	void Start();
	void Stop();
	int  ReadRTP();
	int  ReadRTCP();
	void ProcessRTCPPacket(RTCPCompoundPacket *packet, const char * fromAddr);
	void ReSendPacket(int seq);
	
	int SetRemoteCryptoSDES(const char* suite, const BYTE* key, const DWORD len, int keyRank=0);
	int Run();

private:
	static  void* run(void *par);
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
	
	Streams streams;
	RTPStream * defaultStream;
	
	bool muxRTCP;
	//Sockets
	int 	simSocket;
	int 	simRtcpSocket;
	int 	simPort;
	int	simRtcpPort;
	pollfd	ufds[2];
	bool	inited;
	bool	running;

	DTLSConnection dtls;
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
	char*	iceRemoteUsername;
	char*	iceRemotePwd;
	char*	iceLocalUsername;
	char*	iceLocalPwd;
	pthread_t thread;
	std::mutex mutex;	

	//Tipos
	int 	sendType;

	//Transmision
	sockaddr_in sendAddr;
	sockaddr_in sendRtcpAddr;
	BYTE 	sendPacket[MTU];
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
	in_addr_t recIP;
	in_addr_t iceRemoteIP;
	DWORD	  recPort;

	//RTP Map types
	RTPMap* rtpMapIn;
	RTPMap* rtpMapOut;
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
	bool			sendBitrateFeedback;
	bool			useAbsTime;
	bool 			useOriSeqNum;
	bool 			useOriTS;
	bool 			useExtFIR;
	bool 			useRtcpFIR;

	RTPOrderedPackets	rtxs;
	Use				rtxUse;
	Use				streamUse;
    bool        	resetRequested;
	
	DWORD			lastSendSSRC;

};

#endif
