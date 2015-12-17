#ifndef _TEXTSTREAM_H_
#define _TEXTSTREAM_H_

#include <pthread.h>
#include "config.h"
#include "codecs.h"
#include "rtpsession.h"
#include "text.h"
#include "redcodec.h"
#include <deque>

class TextStream
{
public:
	TextStream(RTPSession::Listener* listener);
	~TextStream();

	int Init(TextInput *input,TextOutput *output);
	int SetTextCodec(TextCodec::Type codec);
	int StartSending(char* sendTextIp,int sendTextPort,RTPMap& rtpMap);
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

	int IsSending()	  { return sendingText;   }
	int IsReceiving() { return receivingText; }
        int GetLocalPort() { return rtp.GetLocalPort(); }
	MediaStatistics GetStatistics();

protected:
	int SendText();
	int RecText();

private:
        RedundentCodec redCodec;

private:
	//Funciones propias
	static void *startSendingText(void *par);
	static void *startReceivingText(void *par);
	TextCodec* CreateTextCodec(TextCodec::Type type);
	//Los objectos gordos
	RTPSession	rtp;
	TextInput	*textInput;
	TextOutput	*textOutput;

	//Parametros del text
	TextCodec::Type textCodec;
	BYTE		t140Codec;
	
	//Las threads
	pthread_t 	recTextThread;
	pthread_t 	sendTextThread;

	//Controlamos si estamos mandando o no
	enum TaskState	sendingText;
	enum TaskState 	receivingText;

	bool		muted;
};
#endif
