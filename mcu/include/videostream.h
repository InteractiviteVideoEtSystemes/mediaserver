#ifndef _VIDEOSTREAM_H_
#define _VIDEOSTREAM_H_

#include <pthread.h>
#include "config.h"
#include "codecs.h"
#include "rtpsession.h"
#include "RTPSmoother.h"
#include "video.h"
#include "logo.h"

class VideoStream 
{
public:
	class Listener : public RTPSession::Listener
	{
	public:
		virtual void onRequestFPU() = 0;
	};
public:
	VideoStream(Listener* listener, Logo & muteLogo,MediaFrame::MediaRole = MediaFrame::VIDEO_MAIN);
	~VideoStream();

	int Init(VideoInput *input, VideoOutput *output);
	void SetRemoteRateEstimator(RemoteRateEstimator* estimator);
	int SetVideoCodec(VideoCodec::Type codec,int mode,int fps,int bitrate,int intraPeriod,const Properties& properties);
	int SetTemporalBitrateLimit(int bitrate);
	int StartSending(char *sendVideoIp,int sendVideoPort,RTPMap& rtpMap);
	int StartSending();
	int StopSending();
	int SendFPU();
	int StartReceiving(RTPMap& rtpMap);
	int StartReceiving();
	int StopReceiving();
	int SetMediaListener(MediaFrame::Listener *listener);
	int SetMute(bool isMuted);
	int SetLocalCryptoSDES(const char* suite, const char* key64);
	int SetRemoteCryptoSDES(const char* suite, const char* key64, int keyRank=0);
	int SetRemoteCryptoDTLS(const char *setup,const char *hash,const char *fingerprint);
	int SetLocalSTUNCredentials(const char* username, const char* pwd);
	int SetRemoteSTUNCredentials(const char* username, const char* pwd);
	int SetRTPProperties(const Properties& properties);
	int End();

	int IsSending()	  { return sendingVideo;  }
	int IsReceiving() { return receivingVideo;}
	MediaStatistics GetStatistics();
	
	void SetVideoInput(VideoInput* input)	{  videoInput = input;	}
	void SetVideoOutput(VideoOutput* output) { videoOutput = output ;}
	VideoOutput* GetVideoOutput() { return videoOutput;}
	
	void SetRTPSession(RTPSession* rtpsess, DWORD newSSRC) { if (rtpSession) rtpSession->CancelGetPacket(recSSRC); rtpSession = rtpsess ; recSSRC = newSSRC ; }
	
protected:
	int SendVideo();
	int RecVideo();

private:
	static void* startSendingVideo(void *par);
	static void* startReceivingVideo(void *par);

	//Listners
	Listener* listener;
	MediaFrame::Listener *mediaListener;

	//Los objectos gordos
	VideoInput     	*videoInput;
	VideoOutput 	*videoOutput;
	RTPSession      rtp;
	RTPSession*     rtpSession;
	RTPSmoother		smoother;
	
	DWORD 	recSSRC;

	//Parametros del video
	VideoCodec::Type videoCodec;		//Codec de envio
	int		videoCaptureMode;	//Modo de captura de video actual
	int 		videoGrabWidth;		//Ancho de la captura
	int 		videoGrabHeight;	//Alto de la captur
	int 		videoFPS;
	int 		videoBitrate;
	int 		videoBitrateLimit;
	int 		videoBitrateLimitCount;
	int		videoIntraPeriod;
	Properties	videoProperties;

	//Las threads
	pthread_t 	sendVideoThread;
	pthread_t 	recVideoThread;
	pthread_mutex_t mutex;
	pthread_cond_t	cond;

	//Controlamos si estamos mandando o no
	enum TaskState sendingVideo;	
	enum TaskState receivingVideo;
	bool	inited;
	bool	sendFPU;
	bool	muted;
	MediaFrame::MediaRole mediaRole;
	Logo & logo;
};

#endif
