#ifndef _AUDIOSTREAM_H_
#define _AUDIOSTREAM_H_

#include <pthread.h>
#include <vector>
#include "config.h"
#include "codecs.h"
#include "rtpsession.h"
#include "audio.h"
#include "dtmfmessage.h"

#define MAX_DTMF_BUFFER 64

class AudioStream
{
public:
class Listener : public RTPSession::Listener
{
public:
	virtual void onDTMF( DTMFMessage* dtmf) = 0;
};
public:
	AudioStream(Listener* listener);
	~AudioStream();

	int Init(AudioInput *input,AudioOutput *output);
	void SetRemoteRateEstimator(RemoteRateEstimator* estimator);
	int SetAudioCodec(AudioCodec::Type codec,const Properties& properties);
	int StartSending(char* sendAudioIp,int sendAudioPort,RTPMap& rtpMap);
	int StopSending();
	int StartReceiving(RTPMap& rtpMap);
	int StopReceiving();
	int SetMute(bool isMuted);
	int SetLocalCryptoSDES(const char* suite, const char* key64);
	int SetRemoteCryptoSDES(const char* suite, const char* key64);
	int SetRemoteCryptoDTLS(const char *setup,const char *hash,const char *fingerprint);
	int SetLocalSTUNCredentials(const char* username, const char* pwd);
	int SetRemoteSTUNCredentials(const char* username, const char* pwd);
	int SetRTPProperties(const Properties& properties);
	int End();

	int IsSending()	  { return sendingAudio;  }
	int IsReceiving() { return receivingAudio;}
	MediaStatistics GetStatistics();
	
	int SendDTMF(DTMFMessage* dtmf);

protected:
	int SendAudio();
	int RecAudio();

private:
	//Funciones propias
	static void *startSendingAudio(void *par);
	static void *startReceivingAudio(void *par);


	Listener* listener;

	//Los objectos gordos
	RTPSession	rtp;
	AudioInput	*audioInput;
	AudioOutput	*audioOutput;

	//Parametros del audio
	AudioCodec::Type audioCodec;
	Properties	 audioProperties;
	
	//Las threads
	pthread_t 	recAudioThread;
	pthread_t 	sendAudioThread;

	pthread_mutex_t mutex;

	//Controlamos si estamos mandando o no
	enum TaskState 	sendingAudio;
	enum TaskState 	receivingAudio;
	std::vector<DTMFMessage*> dtmfBuffer;
	
	bool		muted;

};
#endif
