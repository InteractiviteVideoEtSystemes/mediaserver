#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>
#include "log.h"
#include "tools.h"
#include "audio.h"
#include "audiostream.h"
#include "dtmfmessage.h"

/**********************************
* AudioStream
*	Constructor
***********************************/
AudioStream::AudioStream(Listener* listener) : rtp(MediaFrame::Audio,listener)
{
	sendingAudio=TaskIdle;
	receivingAudio=TaskIdle;
	audioCodec=AudioCodec::PCMU;
	this->listener = listener;
	muted = 0;
	
	//Create objects
	pthread_mutex_init(&mutex,NULL);
}

/*******************************
* ~AudioStream
*	Destructor. 
********************************/
AudioStream::~AudioStream()
{
	pthread_mutex_destroy(&mutex);
}

/***************************************
* SetAudioCodec
*	Fija el codec de audio
***************************************/
int AudioStream::SetAudioCodec(AudioCodec::Type codec,const Properties& properties)
{
	//For DTMF , we still use the main codec
	if (codec == AudioCodec::TELEPHONE_EVENT )
			return 1;
	
	//Colocamos el tipo de audio
	audioCodec = codec;

	//Store properties
	if (! properties.empty() ) audioProperties = properties;

	Log("-SetAudioCodec [%d,%s]. Setting %d properties.\n",audioCodec,AudioCodec::GetNameFor(audioCodec), properties.size());

	//Y salimos
	return 1;	
}

void AudioStream::SetRemoteRateEstimator(RemoteRateEstimator* estimator)
{
	//Set it in the rtp session
	rtp.SetRemoteRateEstimator(estimator);
}

/***************************************
* Init
*	Inicializa los devices 
***************************************/
int AudioStream::Init(AudioInput *input, AudioOutput *output)
{
	Log(">Init audio stream\n");

	//Iniciamos el rtp
	if(!rtp.Init())
		return Error("No hemos podido abrir el rtp\n");
	

	//Nos quedamos con los puntericos
	audioInput  = input;
	audioOutput = output;

	//Y aun no estamos mandando nada
	sendingAudio=TaskIdle;
	receivingAudio=TaskIdle;

	Log("<Init audio stream\n");

	return 1;
}

int AudioStream::SetLocalCryptoSDES(const char* suite, const char* key64)
{
	return rtp.SetLocalCryptoSDES(suite,key64);
}

int AudioStream::SetRemoteCryptoSDES(const char* suite, const char* key64)
{
	return rtp.SetRemoteCryptoSDES(suite,key64);
}

int AudioStream::SetRemoteCryptoDTLS(const char *setup,const char *hash,const char *fingerprint)
{
	return rtp.SetRemoteCryptoDTLS(setup, hash, fingerprint);
}

int AudioStream::SetLocalSTUNCredentials(const char* username, const char* pwd)
{
	return rtp.SetLocalSTUNCredentials(username,pwd);
}

int AudioStream::SetRemoteSTUNCredentials(const char* username, const char* pwd)
{
	return rtp.SetRemoteSTUNCredentials(username,pwd);
}

int AudioStream::SetRTPProperties(const Properties& properties)
{
	//For each property
	for (Properties::const_iterator it=properties.begin();it!=properties.end();++it)
	{
		if (it->first.compare(0, 6, "codec.")==0)
		{
			std::string key = it->first.substr(6, std::string::npos);
			audioProperties[key] = it->second; 
		}
	}
	return rtp.SetProperties(properties);
}
/***************************************
* startSendingAudio
*	Helper function
***************************************/
void * AudioStream::startSendingAudio(void *par)
{
	AudioStream *conf = (AudioStream *)par;
	blocksignals();
	Log("SendAudioThread [%d]\n",getpid());
	pthread_exit((void *)conf->SendAudio());
}

/***************************************
* startReceivingAudio
*	Helper function
***************************************/
void * AudioStream::startReceivingAudio(void *par)
{
	AudioStream *conf = (AudioStream *)par;
	blocksignals();
	Log("RecvAudioThread [%d]\n",getpid());
	pthread_exit((void *)conf->RecAudio());
}

/***************************************
* StartSending
*	Comienza a mandar a la ip y puertos especificados
***************************************/
int AudioStream::StartSending(char *sendAudioIp,int sendAudioPort,RTPMap& rtpMap)
{
	Log(">StartSending audio [%s,%d]\n",sendAudioIp,sendAudioPort);

	//Si estabamos mandando tenemos que parar
	StopSending();
	
	if (sendingAudio != TaskIdle)
		return Error("Cannot start sending audio: bad state.\n");
	
	//Si tenemos audio
	if (sendAudioPort==0)
		//Error
		return Error("Audio port 0\n");


	//Y la de audio
	if(!rtp.SetRemotePort(sendAudioIp,sendAudioPort))
		//Error
		return Error("Error en el SetRemotePort\n");

	//Set sending map
	rtp.SetSendingRTPMap(rtpMap);

	//Set audio codec
	if(!rtp.SetSendingCodec(audioCodec))
		//Error
		return Error("%s audio codec not supported by peer\n",AudioCodec::GetNameFor(audioCodec));

	//Arrancamos el thread de envio
	sendingAudio=TaskStarting;

	//Start thread
	createPriorityThread(&sendAudioThread,startSendingAudio,this,1);

	Log("<StartSending audio [%d]\n",sendingAudio);

	return 1;
}

/***************************************
* StartReceiving
*	Abre los sockets y empieza la recetpcion
****************************************/
int AudioStream::StartReceiving(RTPMap& rtpMap)
{
	//If already receiving
	StopReceiving();
	
	if (receivingAudio != TaskIdle)
		return Error("Failed to start receiving video. Task in bad state.\n");

	
	//Get local rtp port
	int recAudioPort = rtp.GetLocalPort();

	//Set receving map
	rtp.SetReceivingRTPMap(rtpMap);

	//We are reciving audio
	receivingAudio = TaskStarting;

	//Create thread
	createPriorityThread(&recAudioThread,startReceivingAudio,this,1);

	//Log
	Log("<StartReceiving audio [%d]\n",recAudioPort);

	//Return receiving port
	return recAudioPort;
}

/***************************************
* End
*	Termina la conferencia activa
***************************************/
int AudioStream::End()
{
	//Cerramos la session de rtp
	rtp.End();

	//Terminamos de enviar
	StopSending();

	//Y de recivir
	StopReceiving();


	return 1;
}

/***************************************
* StopReceiving
* 	Termina la recepcion
****************************************/

int AudioStream::StopReceiving()
{
	Log(">StopReceiving Audio\n");

	//Y esperamos a que se cierren las threads de recepcion
	if (receivingAudio ==  TaskRunning || receivingAudio ==  TaskStarting  )
	{	
		//Paramos de enviar
		receivingAudio = TaskStopping;

		//Cancel rtp
		rtp.CancelGetPacket();
		
		//Y unimos
		pthread_join(recAudioThread,NULL);
	}

	Log("<StopReceiving Audio\n");

	return 1;

}

/***************************************
* StopSending
* 	Termina el envio
****************************************/
int AudioStream::StopSending()
{
	Log(">StopSending Audio\n");

	//Esperamos a que se cierren las threads de envio
	if (sendingAudio == TaskRunning || sendingAudio == TaskStarting )
	{
		//paramos
		sendingAudio=TaskStopping;

		//Cancel
		audioInput->CancelRecBuffer();

		//Y esperamos
		pthread_join(sendAudioThread,NULL);
	}

	Log("<StopSending Audio\n");

	return 1;	
}


/****************************************
* RecAudio
*	Obtiene los packetes y los muestra
*****************************************/
int AudioStream::RecAudio()
{
	int 		recBytes=0;
	struct timeval 	before;
	SWORD		playBuffer[1024];
	const DWORD	playBufferSize = 1024;
	AudioDecoder*	codec=NULL;
	AudioCodec::Type type;
	DWORD		frameTime=0;
	DWORD		lastTime=0;
	
	Log(">RecAudio\n");
	
	//Inicializamos el tiempo
	gettimeofday(&before,NULL);
	rtp.ResetPacket(false);
	//Mientras tengamos que capturar
	
	receivingAudio = TaskRunning;
	while(receivingAudio == TaskRunning)
	{
		//Obtenemos el paquete
		RTPPacket *packet = rtp.GetPacket();
		//Check
		if (!packet)
		{
			msleep(200);
			//Log("-audiostream: receved null packet.\n");
			//Next
			continue;
		}

		//Get type
		type = (AudioCodec::Type)packet->GetCodec();
		
		
		//Handling DTMF EVENT
		if (type == AudioCodec::TELEPHONE_EVENT )
		{
			DTMFMessage *dtmf = DTMFMessage::Parse(packet);
	
			if (listener && dtmf)
				listener->onDTMF(dtmf);
			
			lastTime = packet->GetTimestamp();
			continue;
		}
		
		//Comprobamos el tipo
		if ((codec==NULL) || (type!=codec->type))
		{
			//Si habia uno nos lo cargamos
			if (codec!=NULL)
				delete codec;

			//Creamos uno dependiendo del tipo
			if ((codec = AudioCodecFactory::CreateDecoder(type))==NULL)
			{
				//Delete pacekt
				delete(packet);
				//Next
				Log("Error creando nuevo codec de audio [%d]\n",type);
				continue;
			}

			//Try to set native pipe rate
			DWORD rate = codec->TrySetRate(audioOutput->GetNativeRate());

			//Start playing at codec rate
			audioOutput->StartPlaying(rate);
		}

		//Lo decodificamos
		int inLen = packet->GetMediaLength();
		int len;
		while ( (len = codec->Decode(packet->GetMediaData(),inLen ,playBuffer,playBufferSize) ) > 0)
		{
			//Obtenemos el tiempo del frame
			frameTime = packet->GetTimestamp() - lastTime;

			//Actualizamos el ultimo envio
			lastTime = packet->GetTimestamp();

			//Check muted
			if (!muted)
				//Y lo reproducimos
				audioOutput->PlayBuffer(playBuffer,len,frameTime);
			inLen = 0; /* loop over to pump all the sound from decoder buffer */
		}


		//Aumentamos el numero de bytes recividos
		recBytes+=packet->GetMediaLength();

		//Delete
		delete(packet);
	}

	//Terminamos de reproducir
	audioOutput->StopPlaying();

	//Borramos el codec
	if (codec!=NULL)
		delete codec;

	receivingAudio = TaskIdle;
	//Salimos
	Log("<RecAudio\n");
	pthread_exit(0);
}

/*******************************************
* SendAudio
*	Capturamos el audio y lo mandamos
*******************************************/
int AudioStream::SendAudio()
{
	RTPPacket	packet(MediaFrame::Audio,audioCodec,audioCodec);
	
	RTPPacket	dtmfpacket(MediaFrame::Audio,AudioCodec::TELEPHONE_EVENT,AudioCodec::TELEPHONE_EVENT);
	SWORD 		recBuffer[512];
        int 		sendBytes=0;
        struct timeval 	before;
	AudioEncoder* 	codec;
	DWORD		frameTime=0;

	Log(">SendAudio\n");

	//Obtenemos el tiempo ahora
	gettimeofday(&before,NULL);

	//Creamos el codec de audio
	if ((codec = AudioCodecFactory::CreateEncoder(audioCodec,audioProperties))==NULL)
		//Error
		return Error("Could not create audio codec\n");

	//Get codec rate
	DWORD rate = codec->TrySetRate(audioInput->GetNativeRate());

	//Start recording at codec rate
	audioInput->StartRecording(rate);


	/*
	   Opus supports 5 different audio bandwidths which may be adjusted
	   during the duration of a call.  The RTP timestamp clock frequency is
	   defined as the highest supported sampling frequency of Opus, i.e.
	   48000 Hz, for all modes and sampling rates of Opus.  The unit for the
	   timestamp is samples per single (mono) channel.  The RTP timestamp
	   corresponds to the sample time of the first encoded sample in the
	   encoded frame.  For sampling rates lower than 48000 Hz the number of
	   samples has to be multiplied with a multiplier according to Table 2
	   to determine the RTP timestamp.

                         +---------+------------+
                         | fs (Hz) | Multiplier |
                         +---------+------------+
                         |   8000  |      6     |
                         |         |            |
                         |  12000  |      4     |
                         |         |            |
                         |  16000  |      3     |
                         |         |            |
                         |  24000  |      2     |
                         |         |            |
                         |  48000  |      1     |
                         +---------+------------+

	 */


	//Get clock rate for codec
	DWORD clock = codec->GetClockRate();

	//Set it
	packet.SetClockRate(clock);
	dtmfpacket.SetClockRate(clock);
	
	//Get ts multiplier
	float multiplier = (float) clock/ (float) rate;
	sendingAudio = TaskRunning;
	//Mientras tengamos que capturar
	while(sendingAudio == TaskRunning)
	{
				
		//Incrementamos el tiempo de envio
		frameTime += codec->numFrameSamples*multiplier;

		//Capturamos 
		if (audioInput->RecBuffer(recBuffer,codec->numFrameSamples)==0)
		{
			Log("-sendingAudio cont\n");
                        msleep(1000);
			continue;
		}

		//Lo codificamos
		int len = codec->Encode(recBuffer,codec->numFrameSamples,packet.GetMediaData(),packet.GetMaxMediaLength());

		//Comprobamos que ha sido correcto
		if(len<=0)
		{
			Log("Error codificando el packete de audio\n");
			continue;
		}

		//Set length
		packet.SetMediaLength(len);

		//Set frametiem
		packet.SetTimestamp(frameTime);

		//Lo enviamos
		rtp.SendPacket(packet,frameTime);
		
		
		//DTMF handling
		pthread_mutex_lock(&mutex);
		while (!dtmfBuffer.empty() )
		{
			DTMFMessage* dtmfmsg = dtmfBuffer.front();
			
			dtmfpacket.SetType(dtmfmsg->type);
			dtmfpacket.SetPayload(dtmfmsg->data,dtmfmsg->size);
			dtmfpacket.SetMark(dtmfmsg->mark);
			
			frameTime += dtmfmsg->size*multiplier;
			
			//Set frametime
			dtmfpacket.SetTimestamp(frameTime);
			
			//Lo enviamos
			rtp.SendPacket(dtmfpacket);
			
			dtmfBuffer.erase(dtmfBuffer.begin());
		}
		
		pthread_mutex_unlock(&mutex);
		
	}

	Log("-SendAudio cleanup[%d]\n",sendingAudio);

	//Paramos de grabar por si acaso
	audioInput->StopRecording();

	//Logeamos
	Log("-Deleting codec\n");

	//Borramos el codec
	delete codec;
	sendingAudio = TaskIdle;
	//Salimos
        Log("<SendAudio\n");
	pthread_exit(0);
}

MediaStatistics AudioStream::GetStatistics()
{
	MediaStatistics stats;

	//Fill stats
        rtp.GetStatistics(0, stats);
	stats.isReceiving	= IsReceiving();
	stats.isSending		= IsSending();

	//Return it
	return stats;
}

int AudioStream::SendDTMF(DTMFMessage* dtmf)
{
	 pthread_mutex_lock(&mutex);
	 dtmfBuffer.push_back(dtmf);
	 pthread_mutex_unlock(&mutex);
}

int AudioStream::SetMute(bool isMuted)
{
	//Set it
	muted = isMuted;
	//Exit
	return 1;
}
