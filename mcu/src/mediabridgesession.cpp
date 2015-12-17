/* 
 * File:   mediabridgesession.cpp
 * Author: Sergio
 * 
 * Created on 22 de diciembre de 2010, 18:20
 */
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include "log.h"
#include "mediabridgesession.h"
#include "codecs.h"
#include "rtpsession.h"
#include "video.h"
#include "h263/h263codec.h"
#include "flv1/flv1codec.h"
#include "avcdescriptor.h"
#include "fifo.h"
#include <wchar.h>
#include <string>


#ifdef FLV1PARSER
#include "flv1/flv1Parser.h"
#endif




//#define CHAT_DEBUG                1


static wchar_t UTF8_T140_CRLF[]   = {0x0d , 0x0a};
static wchar_t UTF8_T140_BS[]        = {0x08};
static wchar_t UTF8_T140_BELL[]      = {0x07};

static BYTE BOMUTF8[]			  = {0xEF,0xBB,0xBF};
static BYTE LOSTREPLACEMENT[]	  = {0xEF,0xBF,0xBD};

#define WLEN(str) 	sizeof(str)/sizeof(wchar_t)

extern DWORD  h264_append_nals(BYTE *dest, DWORD destLen, DWORD destSize, BYTE *buffer, DWORD bufferLen,BYTE **nals,DWORD nalSize,DWORD *num);

MediaBridgeSession::MediaBridgeSession() : 
    rtpAudio(MediaFrame::Audio,NULL), rtpVideo(MediaFrame::Video,NULL), 
    textStream(NULL)
{
	//Neither sending nor receiving
	sendingAudio = Stopped;
	sendingVideo = Stopped;
	receivingAudio = Stopped;
	receivingVideo = Stopped;
	sendFPU = false;
	//Neither transmitter nor receiver
	meta = NULL;
	inited = false;
	//Default values
	rtpVideoCodec = VideoCodec::H264;
	rtpAudioCodec = AudioCodec::PCMU;
	
	//Create default encoders and decoders
	rtpAudioEncoder = NULL;	

	rtpAudioDecoder = AudioCodecFactory::CreateDecoder(AudioCodec::PCMU);
	rtpTextCodec  = TextCodec::T140RED;

	//Create speex encoder and decoder
	rtmpAudioEncoder = AudioCodecFactory::CreateEncoder(AudioCodec::SPEEX16);
	rtmpAudioDecoder = AudioCodecFactory::CreateDecoder(AudioCodec::SPEEX16);


	textInput = new PipeTextInput();
	audioInput = new PipeAudioInput();
	audioOutput = new PipeAudioOutput(false);
	
	audioOutput->Init(rtmpAudioDecoder->GetRate() );	

	SetSendingAudioCodec(rtpAudioCodec);	
	
	eventMngr = NULL;
	queueId=0;

	//Inicializamos los mutex
	pthread_mutex_init(&mutex,NULL);
}

MediaBridgeSession::~MediaBridgeSession()
{
	//End it just in case
	End();
	//Destroy mutex
	pthread_mutex_destroy(&mutex);
	//Delete codecs
	if (rtpAudioEncoder)
		delete(rtpAudioEncoder);
	if (rtpAudioDecoder)
		delete(rtpAudioDecoder);
	if (rtmpAudioEncoder)
		delete(rtmpAudioEncoder);
	if (rtmpAudioDecoder)
		delete(rtmpAudioDecoder);
	if(audioInput)
		delete audioInput;
	if(textInput)
		delete textInput;
}

void MediaBridgeSession::ConfigureAudioInput()
{
   if (audioInput != NULL )
   {
	if ( rtmpAudioDecoder != NULL && rtpAudioEncoder != NULL)
	{
	    audioInput->Init( rtmpAudioDecoder->GetRate() );	
	    DWORD rate = rtpAudioEncoder->TrySetRate(audioInput->GetNativeRate());
	    audioInput->StartRecording( rate );
	    Log("-MediaBridgeSession: reconfigured audio input RTMP sample rate %d -> RTP sample rate %d.\n",
		rtmpAudioDecoder->GetRate(), rate);
	}
	else
	{
	    Error("-MediaBridgeSession: cannot reconfigure audio input.\n");
	}
    }
}

bool MediaBridgeSession::Init(XmlStreamingHandler *eventMngr)
{
	//We are started
	inited = true;
	//Wait for first Iframe
	waitVideo = true;
	//Init rtp
	rtpAudio.Init();
	rtpVideo.Init();

	//Init smoother for video
	smoother.Init(&rtpVideo);
	//Set first timestamp
	getUpdDifTime(&first);

	//Store event mngr
	this->eventMngr = eventMngr;
	
	textInput->Init();
        textStream.Init(textInput, this);
	
	return true;
}

bool MediaBridgeSession::End()
{
	//Check if we are running
	if (!inited)
		return false;

	Log(">MediaBridgeSession end\n");

	//Stop everithing
	StopReceivingAudio();
	StopReceivingVideo();
	StopReceivingText();
	StopSendingAudio();
	StopSendingVideo();
	StopSendingText();

	//Remove meta
	if (meta)
		//delete objet
		delete(meta);
	//No meta
	meta = NULL;

	//Close smoother
	smoother.End();

	//End rtp
	rtpAudio.End();
	rtpVideo.End();

	//Stop
	inited=0;
	
	//Terminamos
	textInput->End();

	Log("<MediaBridgeSession end\n");

	return true;
}

int  MediaBridgeSession::StartSendingVideo(char *sendVideoIp,int sendVideoPort,RTPMap& rtpMap)
{
	Log("-StartSendingVideo [%s,%d]\n",sendVideoIp,sendVideoPort);
	
	switch (sendingVideo)
	{
		case Running:
			Log("-StartSendingVideo : Video  was running,stopping it.\n");
			StopSendingVideo();
			break;
		
		case Starting:
			Log("-StartSendingVideo : Video was already starting, ignoring it.\n");
			return sendingVideo;

		default:
			//Estamos recibiendo
			sendingVideo=Starting;
			break;
	}
	//Si tenemos video
	if (sendVideoPort==0)
		return Error("No video port defined\n");

	//Iniciamos las sesiones rtp de envio
	if(!rtpVideo.SetRemotePort(sendVideoIp,sendVideoPort))
		//Error
		return Error("Error abriendo puerto rtp\n");

	//Set sending map
	rtpVideo.SetSendingRTPMap(rtpMap);

	//Arrancamos los procesos
	createPriorityThread(&sendVideoThread,startSendingVideo,this,0);

	return sendingVideo;
}

int  MediaBridgeSession::StopSendingVideo()
{
	Log(">StopSendingVideo\n");

	//Y esperamos a que se cierren las threads de recepcion
	if (sendingVideo == Running)
	{
		//Dejamos de recivir
		sendingVideo=Stopped;

		//Cancel any pending wait
		videoFrames.Cancel();

		//Esperamos
		pthread_join(sendVideoThread,NULL);
	}

	return true;

	Log("<StopSendingVideo\n");
}

int  MediaBridgeSession::StartReceivingVideo(RTPMap& rtpMap)
{
	//Si estabamos reciviendo tenemos que parar
	int recPort = rtpVideo.GetLocalPort();

	switch (receivingVideo)
	{
		case Running:
			Log("-StartReceivingVideo : Video was running,stopping it.\n");
			StopReceivingVideo();
			break;
		
		case Starting:
			Log("-StartReceivingVideo : Video was already starting, ignoring it. Returning same port%d.\n", recPort);
			return recPort;

		default:
			//Estamos recibiendo
			receivingVideo=Starting;
			break;
	}
	//Set receving map
	rtpVideo.SetReceivingRTPMap(rtpMap);
	
	//Arrancamos los procesos
	createPriorityThread(&recVideoThread,startReceivingVideo,this,0);

	//Logeamos
	Log("-StartReceivingVideo Port [%d]\n",recPort);

	return recPort;
}

int  MediaBridgeSession::StopReceivingVideo()
{
	Log(">StopReceivingVideo\n");

	//Y esperamos a que se cierren las threads de recepcion
	if (receivingVideo == Running)
	{
		//Dejamos de recivir
		receivingVideo=Stopped;
		
		//Cancel the rtp
		rtpVideo.CancelGetPacket();
		
		//Esperamos
		pthread_join(recVideoThread,NULL);
	}

	Log("<StopReceivingVideo\n");

	return 1;
}

int  MediaBridgeSession::StartSendingAudio(char *sendAudioIp,int sendAudioPort,RTPMap& rtpMap)
{
	Log("-StartSendingAudio [%s,%d]\n",sendAudioIp,sendAudioPort);
	
	switch (sendingAudio)
	{
		case Running:
			Log("-StartSendingAudio : Audio  was running,stopping it.\n");
			StopSendingAudio();
			break;
		
		case Starting:
			Log("-StartSendingAudio : Audio was already starting, ignoring it.\n");
			return sendingAudio;

		default:
			//Estamos recibiendo
			sendingAudio=Starting;
			break;
	}
	
	//Si tenemos Audio
	if (sendAudioPort==0)
		return Error("No Audio port defined\n");

	//Iniciamos las sesiones rtp de envio
	if(!rtpAudio.SetRemotePort(sendAudioIp,sendAudioPort))
		return Error("Error abriendo puerto rtp\n");

	//Set sending map
	rtpAudio.SetSendingRTPMap(rtpMap);

	//Set default codec
	rtpAudio.SetSendingCodec(rtpAudioCodec);

	//Arrancamos los procesos
	createPriorityThread(&sendAudioThread,startSendingAudio,this,0);

	return sendingAudio;
}

int  MediaBridgeSession::StopSendingAudio()
{
	Log(">StopSendingAudio\n");

	//Y esperamos a que se cierren las threads de recepcion
	if (sendingAudio == Running)
	{
		//Dejamos de recivir
		sendingAudio=Stopped;

		
		//Cancel any pending wait
		audioInput->CancelRecBuffer();

		//Esperamos
		pthread_join(sendAudioThread,NULL);
	}

	Log("<StopSendingAudio\n");

	return 1;
}

int  MediaBridgeSession::StartReceivingAudio(RTPMap& rtpMap)
{
	//Si estabamos reciviendo tenemos que parar
	int recPort = rtpAudio.GetLocalPort();

	switch (receivingAudio)
	{
		case Running:
			Log("-StartReceivingAudio : Audio was running,stopping it.\n");
			StopReceivingAudio();
			break;
		
		case Starting:
			Log("-StartReceivingAudio : Audio was already starting, ignoring it. Returning same port%d.\n", recPort);
			return recPort;

		default:
			//Estamos recibiendo
			receivingAudio=Starting;
			break;
	}
	
	//Set receving map
	rtpAudio.SetReceivingRTPMap(rtpMap);

	//Arrancamos los procesos
	createPriorityThread(&recAudioThread,startReceivingAudio,this,0);
	
	//Logeamos
	Log("-StartReceivingAudio port=[%d]\n",recPort);

	return recPort;
}

int  MediaBridgeSession::StopReceivingAudio()
{
	Log(">StopReceivingAudio\n");

	//Y esperamos a que se cierren las threads de recepcion
	if (receivingAudio == Running )
	{
		//Dejamos de recivir
		receivingAudio=Stopped;

		//Cancel audio rtp get packet
		rtpAudio.CancelGetPacket();
		
		//Esperamos
		pthread_join(recAudioThread,NULL);
	}

	Log("<StopReceivingAudio\n");

	return 1;
}

int  MediaBridgeSession::StartSendingText(char *sendTextIp,int sendTextPort,RTPMap& rtpMap)
{
	Log("-StartSendingText [%s,%d]\n",sendTextIp,sendTextPort);

	switch (sendingText)
	{
		case Running:
			Log("-StartSendingText : Text  was running,stopping it.\n");
			StopSendingText();
			break;
		
		case Starting:
			Log("-StartSendingText : Text was already starting, ignoring it.\n");
			return sendingText;

		default:
			//Estamos recibiendo
			sendingText=Starting;
			break;
	}
	//Si tenemos Text
	if (sendTextPort==0)
		return Error("No Text port defined\n");

        //Get t140 payload type for redundancy
        t140Codec = TextCodec::T140;
	for (RTPMap::iterator it = rtpMap.begin(); it!=rtpMap.end(); ++it)
	{
		//Is it ourr codec
		if (it->second==TextCodec::T140)
		{
			//Set it
			t140Codec = it->first;
			//and we are done
			continue;
		}
	}

	if(!textStream.SetTextCodec(rtpTextCodec))
		//Error
		return Error("Text codec [%s] not supported by peer\n",
                        TextCodec::GetNameFor(rtpTextCodec));
        
        int ret = textStream.StartSending(sendTextIp, sendTextPort, rtpMap);

        if (ret) sendingText = Running;
        
	return ret;
}

int  MediaBridgeSession::StopSendingText()
{
	Log(">StopSendingText\n");

	//Check
	if (sendingText == Running || sendingText == Starting)
	{
            sendingText = Stopping;
            textStream.StopSending();
            //Dejamos de recivir
            sendingText=Stopped;
	
	}	
	Log("<StopSendingText\n");

	return 1;
}

int  MediaBridgeSession::StartReceivingText(RTPMap& rtpMap)
{
	//Si estabamos reciviendo tenemos que parar

	switch (receivingText)
	{
		case Running:
			Log("-StartReceivingtext : Text was running,stopping it.\n");
			StopReceivingText();
			break;
		
		case Starting:
			Log("-StartReceivingtext : Text was already starting, ignoring it. Returning same port.\n");
			break;

		default:
			//Estamos recibiendo
			receivingText = Starting;
			break;
	}
	
        int recPort = textStream.StartReceiving(rtpMap);
        receivingText = Running;
	//Logeamos
	Log("-StartReceivingText Port [%d]\n",recPort);

	return recPort;
}

int  MediaBridgeSession::StopReceivingText()
{
	Log(">MediaBridgeSession StopReceivingText\n");

	//Y esperamos a que se cierren las threads de recepcion
	if (receivingText == Running || receivingText == Stopping)
	{
		//Dejamos de recivir
		receivingText=Stopping;
                textStream.StopSending();
                receivingText=Stopped;
	}

	Log("<MediaBridgeSession StopReceivingText\n");

	return 1;
}

/**************************************
* startReceivingVideo
*	Function helper for thread
**************************************/
void* MediaBridgeSession::startReceivingVideo(void *par)
{
	Log("RecVideoThread [%d]\n",getpid());

	//Obtenemos el objeto
	MediaBridgeSession *sess = (MediaBridgeSession *)par;

	//Bloqueamos las se�a�es
	blocksignals();

	//Y ejecutamos
	pthread_exit( (void *)sess->RecVideo());
}

/**************************************
* startReceivingAudio
*	Function helper for thread
**************************************/
void* MediaBridgeSession::startReceivingAudio(void *par)
{
	Log("RecVideoThread [%d]\n",getpid());

	//Obtenemos el objeto
	MediaBridgeSession *sess = (MediaBridgeSession *)par;

	//Bloqueamos las se�a�es
	blocksignals();

	//Y ejecutamos
	pthread_exit( (void *)sess->RecAudio());
}

/**************************************
* startSendingVideo
*	Function helper for thread
**************************************/
void* MediaBridgeSession::startSendingVideo(void *par)
{
	Log("SendVideoThread [%d]\n",getpid());

	//Obtenemos el objeto
	MediaBridgeSession *sess = (MediaBridgeSession *)par;

	//Bloqueamos las se�a�es
	blocksignals();

	//Y ejecutamos
	pthread_exit( (void *)sess->SendVideo());
}

/**************************************
* startSendingAudio
*	Function helper for thread
**************************************/
void* MediaBridgeSession::startSendingAudio(void *par)
{
	Log("SendAudioThread [%d]\n",getpid());

	//Obtenemos el objeto
	MediaBridgeSession *sess = (MediaBridgeSession *)par;

	//Bloqueamos las se�a�es
	blocksignals();

	//Y ejecutamos
	pthread_exit( (void *)sess->SendAudio());
}

/****************************************
* RecVideo
*	Obtiene los packetes y los muestra
*****************************************/
int MediaBridgeSession::RecVideo()
{
	//Coders
	VideoDecoder* decoder = NULL;
	VideoEncoder* encoder = VideoCodecFactory::CreateEncoder(VideoCodec::SORENSON);
	//Create new video frame
	RTMPVideoFrame  frame(0,65500);
	//Set codec
	frame.SetVideoCodec(RTMPVideoFrame::FLV1);

	int 	width=0;
	int 	height=0;
	DWORD	numpixels=0;
	
	Log(">RecVideo\n");
	receivingVideo = Running;
	//Mientras tengamos que capturar
	while(receivingVideo == Running)
	{
		///Obtenemos el paquete
		RTPPacket* packet = rtpVideo.GetPacket();

		//Check
		if (!packet)
			//Next
			continue;

		//Get type
		VideoCodec::Type type = (VideoCodec::Type)packet->GetCodec();


		if ((decoder==NULL) || (type!=decoder->type))
		{
			//Si habia uno nos lo cargamos
			if (decoder!=NULL)
				delete decoder;

			//Creamos uno dependiendo del tipo
			decoder = VideoCodecFactory::CreateDecoder(type);

			//Check
			if (!decoder)
				continue;
		}

		//Lo decodificamos
		if(!decoder->DecodePacket(packet->GetMediaData(),packet->GetMediaLength(),0,packet->GetMark()))
		{
			delete(packet);
			continue;
		}
		//Get mark
		bool mark = packet->GetMark();

		//Delete packet
		delete(packet);

		//Check if it is last one
		if(!mark)
			continue;
	
		//Check size
		if (decoder->GetWidth()!=width || decoder->GetHeight()!=height)
		{
			//Get dimension
			width = decoder->GetWidth();
			height = decoder->GetHeight();

			//Set size
			numpixels = width*height*3/2;

			//Set also frame rate and bps
			encoder->SetFrameRate(25,300,500);

			//Set them in the encoder
			encoder->SetSize(width,height);
		}

		//Encode next frame
		VideoFrame *encoded = encoder->EncodeFrame(decoder->GetFrame(),numpixels);

		//Check
		if (!encoded)
			break;

		//Check size
		if (frame.GetMaxMediaSize()<encoded->GetLength())
			//Not enougth space
			return Error("Not enought space to copy FLV encodec frame [frame:%d,encoded:%d",frame.GetMaxMediaSize(),encoded->GetLength());

		//Get full frame
		frame.SetVideoFrame(encoded->GetData(),encoded->GetLength());

		//Set buffer size
		frame.SetMediaSize(encoded->GetLength());

		//Check type
		if (encoded->IsIntra())
			//Set type
			frame.SetFrameType(RTMPVideoFrame::INTRA);
		else
			//Set type
			frame.SetFrameType(RTMPVideoFrame::INTER);

		//Let the connection set the timestamp
		frame.SetTimestamp(getDifTime(&first)/1000);

		//Send it
		SendMediaFrame(&frame);
	}

	//Check
	if (decoder)
		//Delete
		delete(decoder);
	//Check
	if (encoder)
		//Delete
		delete(encoder);

	Log("<RecVideo\n");

	//Salimos
	pthread_exit(0);
}

/****************************************
* RecAudio
*	Obtiene los packetes y los muestra
*****************************************/
int MediaBridgeSession::RecAudio()
{
	DWORD		firstAudio = 0;
	DWORD		timeStamp=0;
	DWORD		firstTS = 0;
	SWORD		raw[512];
	DWORD		rawSize = 512;
	DWORD		rawLen;

	//Create new audio frame
	RTMPAudioFrame  *audio = new RTMPAudioFrame(0,MTU);
	
	audioOutput->StartPlaying(rtpAudioDecoder->GetRate());
	Log(">RecAudio\n");
	receivingAudio = Running;
	//Mientras tengamos que capturar
	while(receivingAudio == Running)
	{
		//Obtenemos el paquete
		RTPPacket *packet = rtpAudio.GetPacket();
		
		//Check
		if (!packet)
			//Next
			continue;
		
		//Get type
		AudioCodec::Type codec = (AudioCodec::Type)packet->GetCodec();

		//Check rtp type
		if (codec==AudioCodec::SPEEX16)
		{
			//TODO!!!!
		}

		//Check if we have a decoder
		if (!rtpAudioDecoder || rtpAudioDecoder->type!=codec)
		{
			//Check
			if (rtpAudioDecoder)
				//Delete old one
				delete(rtpAudioDecoder);
			//Create new one
			rtpAudioDecoder = AudioCodecFactory::CreateDecoder(codec);
			audioOutput->StartPlaying(rtpAudioDecoder->GetRate());
		}

		//Decode it
		rawLen = rtpAudioDecoder->Decode(packet->GetMediaData(),packet->GetMediaLength(),raw,rawSize);

		//Delete packet
		delete(packet);
					
		//Enqeueue it
		audioOutput->PlayBuffer(raw,rawLen,0);
		
		//Rencode it
			
		while ( rawLen = audioOutput->GetSamples(raw, rtmpAudioEncoder->numFrameSamples,true) )
		{
		    DWORD len;
		    while((len=rtmpAudioEncoder->Encode(raw,rawLen,audio->GetMediaData(),audio->GetMaxMediaSize()))>0)
		    {
			//REset
			rawLen = 0;
			
			//Set length
			audio->SetMediaSize(len);
		
			switch(rtmpAudioEncoder->type)
			{
				case AudioCodec::SPEEX16:
					//Set RTMP data
					audio->SetAudioCodec(RTMPAudioFrame::SPEEX);
					audio->SetSoundRate(RTMPAudioFrame::RATE11khz);
					audio->SetSamples16Bits(1);
					audio->SetStereo(0);
					break;
				case AudioCodec::NELLY8:
					//Set RTMP data
					audio->SetAudioCodec(RTMPAudioFrame::NELLY8khz);
					audio->SetSoundRate(RTMPAudioFrame::RATE11khz);
					audio->SetSamples16Bits(1);
					audio->SetStereo(0);
					break;
				case AudioCodec::NELLY11:
					//Set RTMP data
					audio->SetAudioCodec(RTMPAudioFrame::NELLY);
					audio->SetSoundRate(RTMPAudioFrame::RATE11khz);
					audio->SetSamples16Bits(1);
					audio->SetStereo(0);
					break;
			}

			//If it is first
			if (!firstTS)
			{
				//Get first audio time
				firstAudio = getDifTime(&first)/1000;
				//It is first
				firstTS = timeStamp;
			}

			DWORD ts = firstAudio +(timeStamp-firstTS)/8;
			//Set timestamp
			audio->SetTimestamp(ts);

			//Send packet
			SendMediaFrame(audio);
		    }
		}
	}

			
	audioOutput->StopPlaying();
	//Check
	if (audio)
		//Delete it
		delete(audio);
	
	Log("<RecAudio\n");

	//Salimos
	pthread_exit(0);
}



void MediaBridgeSession::SendTextToFlashClient( std::wstring  wstext )
{
	
	TextCommands    cmd = NIL;
	//Create new timestamp associated to latest media time
	RTMPMetaData meta(getDifTime(&first)/1000);

	//Add text name
	meta.AddParam(new AMFString(L"onText"));

        //Search commands in the text flow
	for (int i=0; i<wstext.length() && i < PATH_MAX; i++) 
	{
		//Get char
			 wchar_t ch = wstext.at(i);
			//Depending on the char

		switch (ch)
		{
		
			case 0x9b:
				
				cmd = ESC;
				break;
				
			case 0x0D:
			case 0x2028:
			case 0x2029:

				cmd = NL;
				break;
				 
			case 0x08:

				cmd = BS;
				break;

			case 0x07:

				cmd = BEL;
				break;
					
			case 0xFEFF:

				cmd = BOM;
				break;

			default:
				break;
		}
	
		if (cmd != NIL  )
		{

			if( wstext.substr(0,i).length() > 0  )
			{
				//Set data
				meta.AddParam(new AMFString(wstext.substr(0,i)));
				
				//Send data to flash client
			SendMetaData(&meta);
			}
		
			cmd = NIL;

			if ( i+1 <= wstext.length() )
			{
				wstext=wstext.substr(i+1);
				i=0;
			}
		}
		else if( i == wstext.length()-1 )
		{
			//Set data
			meta.AddParam(new AMFString(wstext));

				//Send data to flash client
			SendMetaData(&meta);
		}
	}
}


void MediaBridgeSession::SendTextToHtmlClient( std::wstring wstext )
{
	TextCommands    cmd = NIL;

#ifdef CHAT_DEBUG
        Log("RecText(): text(%ls) to process size=%i\n",wstext.c_str(),wstext.length() );
#endif
	int i=0;
        //Search commands in the text flow
	while( i<wstext.length() && i < PATH_MAX) 
        {
        	 bool reset_text=false;
		//Get char
                 wchar_t ch = wstext.at(i);
                //Depending on the char
#ifdef CHAT_DEBUG
		Log("char[%d] 0x%x to process.\n",i,ch);
#endif
		switch (ch)
		{
		
			case 0x9b:
				#ifdef CHAT_DEBUG
				Log("receiving ESCAPE Character.\n");
#endif
				cmd = ESC;
				break;
				
			case 0x0D:
			case 0x2028:
			case 0x2029:
#ifdef CHAT_DEBUG
				Log("receiving t140 NEWLINE command.\n");
#endif
				cmd = NL;
				break;
				 
            case 0x08:
#ifdef CHAT_DEBUG
				Log("receiving t140 command BS\n");
#endif
				cmd = BS;
				break;

			case 0x07:
#ifdef CHAT_DEBUG
				Log("receiving t140 command BEL\n");
#endif
				cmd = BEL;
				break;
					
			case 0xFEFF:
#ifdef CHAT_DEBUG
				Log("receiving t140 command BOM\n");
#endif
				cmd = BOM;
                               	break;

			default:
				break;
		}
		if (cmd == ESC  )
		{
			wstext=wstext.substr(i+1);
			break;
		}
		else
		{
			if (cmd != NIL  )
			{

				if( wstext.substr(0,i).length() > 0  )
				{
#ifdef CHAT_DEBUG
					Log("RecText(): flushing buffer text(%ls) with a length of %i to queueId=%i\n",wstext.substr(0,i).c_str(),wstext.substr(0,i).length(), this->queueId);
#endif
					eventMngr->AddEvent(this->queueId, new ::OnTextReceivedEvent(this->sessionId, wstext.substr(0,i) ));
				}
#ifdef CHAT_DEBUG
				Log("RecText(): receiving cmd to queueId=%i\n", this->queueId);
#endif

			
				eventMngr->AddEvent(this->queueId, new ::OnCmdReceivedEvent(this->sessionId, cmd ));
				
				cmd = NIL;

				if ( i+1 <= wstext.length() )
				{
					wstext=wstext.substr(i+1);
					i=0;
					reset_text = true;
				}
			}
			else if( i == wstext.length()-1 )
			{

#ifdef CHAT_DEBUG
				Log("RecText(): receiving text(%ls) to queueId=%i\n",wstext.c_str(), this->queueId);
#endif
				eventMngr->AddEvent(this->queueId, new ::OnTextReceivedEvent(this->sessionId, wstext ));
			}
		}
		if (!reset_text)
			i++;
	}
	
	if (cmd == ESC)
			eventMngr->AddEvent(this->queueId, new ::OnCSIANSIReceivedEvent(this->sessionId, wstext ));
}


int MediaBridgeSession::SendFrame(TextFrame &frame)
{
    SendTextToHtmlClient(frame.GetWString());
    SendTextToFlashClient(frame.GetWString());
}

int MediaBridgeSession::SetSendingVideoCodec(VideoCodec::Type codec)
{
	//Store it
	rtpVideoCodec = codec;
	//OK
	return 1;
}

int MediaBridgeSession::SetSendingAudioCodec(AudioCodec::Type codec)
{
	//Store it
	rtpAudioCodec = codec;
	if (rtpAudioEncoder)
		//Delete old one
		delete(rtpAudioEncoder);
		
	rtpAudioEncoder = AudioCodecFactory::CreateEncoder(rtpAudioCodec);
	
	ConfigureAudioInput();
	
	return 1;
}

int MediaBridgeSession::SetSendingTextCodec(TextCodec::Type codec)
{
	//Store it
	rtpTextCodec = codec;
	//OK
	return 1;
}


int MediaBridgeSession::SendFPU()
{
	//Set it
	sendFPU = true;

	//Exit
	return 1;
}

int MediaBridgeSession::SendVideo()
{
	VideoCodec::Type rtmpVideoCodec;
	
	VideoDecoder *decoder = VideoCodecFactory::CreateDecoder(VideoCodec::SORENSON);
	VideoEncoder *encoder = VideoCodecFactory::CreateEncoder(rtpVideoCodec);
	DWORD width = 0;
	DWORD height = 0;
	DWORD numpixels = 0;
	
	QWORD	lastVideoTs = 0;
	
	Log(">SendVideo\n");

	//Set video format
	if (!rtpVideo.SetSendingCodec(rtpVideoCodec))
		//Error
		return Error("Peer do not support [%d,%s]\n",rtpVideoCodec,VideoCodec::GetNameFor(rtpVideoCodec));
	sendingVideo = Running;
	//While sending video
	while (sendingVideo == Running)
	{
		//Wait for next video
		if (!videoFrames.Wait(0))
			//Check again
			continue;

		//Get audio grame
		RTMPVideoFrame* video = videoFrames.Pop();
		//check
		if (!video)
			//Again
			continue;

		//Get time difference
		DWORD diff = 0;
		//Get timestam
		QWORD ts = video->GetTimestamp();
		//If it is not the first frame
		if (lastVideoTs)
			//Calculate it
			diff = ts - lastVideoTs;
		//Set the last audio timestamp
		lastVideoTs = ts;

		//Get codec type
		//Check type to find decoder
		switch(video->GetVideoCodec())
		{
			case RTMPVideoFrame::VP6:
				rtmpVideoCodec = VideoCodec::VP6;
				break;
			case RTMPVideoFrame::FLV1:
				rtmpVideoCodec = VideoCodec::SORENSON;
				break;
			case RTMPVideoFrame::AVC:
				rtmpVideoCodec = VideoCodec::H264;
				break;
			default:
				Log("mediabridgesession: unknown video frame received. Ignoring.\n");
				//Not found, ignore
				continue;
		}
		
		//Check rtp type
		if (rtpVideoCodec != rtmpVideoCodec)
		{
			
			//Check if we have a decoder
			if (!decoder || decoder->type!=rtmpVideoCodec)
			{
				//Check
				if (decoder)
					//Delete old one
					delete(decoder);
				//Create new one
				decoder = VideoCodecFactory::CreateDecoder(rtmpVideoCodec);			
			}
		}

		//Decode frame
		if (!decoder->Decode(video->GetMediaData(),video->GetMediaSize()))
		{
			Error("mediabridge session: decode video packet error");
			//Next
			continue;
		}

		//Check size
		if (decoder->GetWidth()!=width || decoder->GetHeight()!=height)
		{
			//Get dimension
			width = decoder->GetWidth();
			height = decoder->GetHeight();

			//Set size
			numpixels = width*height*3/2;

			//Set also frame rate and bps
			encoder->SetFrameRate(25,300,500);

			//Set them in the encoder
			encoder->SetSize(width,height);
		}
		//Check size
		if (!numpixels)
		{
			Error("numpixels equals 0");
			//Next
			continue;
		}
		//Check fpu
		if (sendFPU)
		{
			//Send it
			encoder->FastPictureUpdate();
			//Reset
			sendFPU = false;
		}

		//Encode it
		VideoFrame *videoFrame = encoder->EncodeFrame(decoder->GetFrame(),numpixels);

		//If was failed
		if (!videoFrame)
		{
			Log("No video frame\n");
			//Next
			continue;
		}

		//Set frame time
		videoFrame->SetTimestamp(diff);

		//Send it smoothly
		smoother.SendFrame(videoFrame,diff);

		//Delete video frame
		delete(video);
	}

	Log("<SendVideo\n");

	return 1;
}

int MediaBridgeSession::SendAudio()
{
	QWORD lastAudioTs = 0;
	DWORD frameTime=0;
	SWORD recBuffer[512];
	
	Log(">SendAudio\n");
	sendingAudio = Running;
	//While sending audio
	while (sendingAudio == Running)
	{
		RTPPacket packet(MediaFrame::Audio,rtpAudioCodec);
		//Incrementamos el tiempo de envio
		frameTime += rtpAudioEncoder->numFrameSamples;

		if (audioInput->RecBuffer(recBuffer,rtpAudioEncoder->numFrameSamples)==0)
		{
			Log("-sendingAudio cont\n");
			continue;
		}

		//Lo codificamos
		int len = rtpAudioEncoder->Encode(recBuffer,rtpAudioEncoder->numFrameSamples,packet.GetMediaData(),packet.GetMaxMediaLength());
		//Set length
		packet.SetMediaLength(len);

		//Set frametiem
		packet.SetTimestamp(frameTime);

		//Lo enviamos
		rtpAudio.SendPacket(packet,frameTime);				
	}

	Log("<SendAudio\n");

	return 1;
}

void MediaBridgeSession::AddInputToken(const std::wstring &token)
{
	//Add token
	inputTokens.insert(token);
}
void MediaBridgeSession:: AddOutputToken(const std::wstring &token)
{
	//Add token
	outputTokens.insert(token);
}

bool MediaBridgeSession::ConsumeInputToken(const std::wstring &token)
{
	//Check token
	ParticipantTokens::iterator it = inputTokens.find(token);

	//Check we found one
	if (it==inputTokens.end())
		//Not found
		return Error("Participant token not found\n");

	//Remove token
	inputTokens.erase(it);

	//Ok
	return true;
}

bool MediaBridgeSession::ConsumeOutputToken(const std::wstring &token)
{
	//Check token
	ParticipantTokens::iterator it = outputTokens.find(token);

	//Check we found one
	if (it==outputTokens.end())
		//Not found
		return Error("Participant token not found\n");

	//Remove token
	outputTokens.erase(it);

	//Ok
	return true;
}

DWORD MediaBridgeSession::AddMediaListener(RTMPMediaStream::Listener *listener)
{
	//Check inited
	if (!inited)
		//Exit
		return Error("RTMP participant not inited when trying to add listener\n");

	//Check listener
	if (!listener)
		//Do not add
		return GetNumListeners();

	//Call parent
	DWORD num = RTMPMediaStream::AddMediaListener(listener);

	//Init
	listener->onStreamBegin(RTMPMediaStream::id);

	//If we already have metadata
	if (meta)
		//Send it
		listener->onMetaData(RTMPMediaStream::id,meta);

	//Return number of listeners
	return num;
}

DWORD MediaBridgeSession::RemoveMediaListener(RTMPMediaStream::Listener *listener)
{
	//Call parent
	DWORD num = RTMPMediaStream::RemoveMediaListener(listener);

	//Return number of listeners
	return num;
}

void MediaBridgeSession::onAttached(RTMPMediaStream *stream)
{
	Log("-RTMP media bridged attached to stream [id:%d]\n",stream?stream->GetStreamId():-1);

}

bool MediaBridgeSession::onMediaFrame(DWORD id,RTMPMediaFrame *frame)
{
	AudioCodec::Type rtmpAudioCodec;
	RTMPAudioFrame* audio;
	QWORD ts;
	QWORD diff = 0;
	bool ret = true;
	//Depending on the type
	switch (frame->GetType())
	{
		case RTMPMediaFrame::Video:
			//Push it
			if (videoFrames.Length() > 20)
				ret = false;
			videoFrames.Add((RTMPVideoFrame*)(frame->Clone()));
			break;
		case RTMPMediaFrame::Audio:


			audio = (RTMPAudioFrame*) frame;
			//Get timestamp
			ts = audio->GetTimestamp();
		//Get delay
		
		//If it is the first frame
		/*if (lastAudioTs)
			//Calculate it
			diff = ts - lastAudioTs;
		//Check diff
		if (diff<40)
			//Set it to only one frame of 20 ms
			diff = 20;
		//Set the last audio timestamp
		lastAudioTs = ts; */

		//Get codec type
			switch(audio->GetAudioCodec())
			{
				case RTMPAudioFrame::SPEEX:
					//Set codec
					rtmpAudioCodec = AudioCodec::SPEEX16;
					break;

				case RTMPAudioFrame::NELLY:
					//Set codec type
					rtmpAudioCodec = AudioCodec::NELLY11;
					break;

				case RTMPAudioFrame::G711U:
					rtmpAudioCodec = AudioCodec::PCMU;
					break;

				case RTMPAudioFrame::G711A:
					rtmpAudioCodec = AudioCodec::PCMA;
					break;

				default:
					return true;;
			}
			//Check rtp type
			if (rtpAudioCodec != rtmpAudioCodec)
			{
				SWORD raw[512];
				DWORD rawSize = sizeof(raw)/sizeof(SWORD);
				DWORD rawLen = 0;
				//Check if we have a decoder
				if (!rtmpAudioDecoder || rtmpAudioDecoder->type!=rtmpAudioCodec)
				{
					//Check
					if (rtmpAudioDecoder)
						//Delete old one
						delete(rtmpAudioDecoder);
					//Create new one
					rtmpAudioDecoder = AudioCodecFactory::CreateDecoder(rtmpAudioCodec);
					ConfigureAudioInput();
				}
			
				//Decode it until no frame is found
				DWORD size = audio->GetMediaSize();
				while ((rawLen = rtmpAudioDecoder->Decode(audio->GetMediaData() , size, raw, rawSize)) >0 )
				{
					audioInput->PutSamples(raw, rawLen);
					size = 0;
				}
				break;
			}
			else
			{
				// To do: do not decode and send the RTP packet from here
			}
			break;

		default:
			break;
	}
	return ret;
}

void MediaBridgeSession::onMetaData(DWORD id,RTMPMetaData *publishedMetaData)
{
	//Cheeck
	if (!sendingText)
		//Exit
		return;

	//Get name
	AMFString* name = (AMFString*)publishedMetaData->GetParams(0);

	//Check it the send command text
	if (name->GetWString().compare(L"onText")==0)
	{
		RTPPacket packet(MediaFrame::Text,TextCodec::T140);
		
		//Get string
		AMFString *str = (AMFString*)publishedMetaData->GetParams(1);

		//Log
		Log("Sending t140 data [\"%ls\"]\n",str->GetWChar());
		textInput->WriteText(str->GetWString());       	
	}
}


void MediaBridgeSession::SendTextInput(char* mess)
{
	int i;
	UTF8Parser parser;
	
	//Cheeck
	if (sendingText == Stopped)
		//Exit
		return;
	
	parser.Parse((BYTE*)mess,strlen(mess));
	textInput->WriteText(parser.GetWString());
	
}

void MediaBridgeSession::SendSpecial(char* cmd, int nbCmd)
{
	//Cheeck
	if (sendingText == Stopped)
		//Exit
		return;
		
	wchar_t* txt ;
	std::wstring sendTxt  ;
	unsigned int len = 0;
	//Log
	if (strcmp(cmd,"NL") == 0)
	{
#ifdef CHAT_DEBUG
		Log("Sending t140 command %i * NL\n",nbCmd);
#endif
		txt = UTF8_T140_CRLF;
		len = WLEN(UTF8_T140_CRLF);
	}
	else if (strcmp(cmd,"BS") == 0)
	{
#ifdef CHAT_DEBUG
		Log("Sending t140 command %i * BS\n",nbCmd);
#endif
		txt = UTF8_T140_BS;
		len = WLEN(UTF8_T140_BS);
	}
	else if (strcmp(cmd,"BEL") == 0)
	{
#ifdef CHAT_DEBUG
		Log("Sending t140 command %i * BEL\n",nbCmd);
#endif
		txt = UTF8_T140_BELL;
		len = WLEN(UTF8_T140_BELL);
	}
	else
	{ 
		Error("Unknown cmd %ls",cmd); 
		return;
	}

	for (int i=0; i<nbCmd; i++)
	{
		sendTxt.append(txt, len);
	}
	textInput->WriteText(sendTxt);
}	

void MediaBridgeSession::onCommand(DWORD id,const wchar_t *name,AMFData* obj)
{

}

void MediaBridgeSession::onStreamBegin(DWORD id)
{

}

void MediaBridgeSession::onStreamEnd(DWORD id)
{

}

void MediaBridgeSession::onStreamReset(DWORD id)
{

}

void MediaBridgeSession::onDetached(RTMPMediaStream *stream)
{
	Log("-RTMP media bridge detached from stream [id:%d]\n",stream?stream->GetStreamId():-1);
}

/*****************************************************
 * RTMP NetStream
 *
 ******************************************************/
MediaBridgeSession::NetStream::NetStream(DWORD streamId,MediaBridgeSession *sess,RTMPNetStream::Listener* listener) : RTMPNetStream(streamId,listener)
{
	//Store conf
	this->sess = sess;
}

MediaBridgeSession::NetStream::~NetStream()
{
	//Close
	Close();
	///Remove listener just in case
	RemoveAllMediaListeners();
}

void MediaBridgeSession::NetStream::doPlay(std::wstring& url,RTMPMediaStream::Listener* listener)
{
	//Log
	Log("-Play stream [%ls]\n",url.c_str());

	//Remove extra data from FMS
	if (url.find(L"*flv:")==0)
		//Erase it
		url.erase(0,5);
	else if (url.find(L"flv:")==0)
		//Erase it
		url.erase(0,4);

	//Check token
	if (!sess->ConsumeOutputToken(url))
	{
		//Send error
		fireOnNetStreamStatus(RTMP::Netstream::Play::StreamNotFound,L"Token invalid");
		//Exit
		return;
	}

	//Send reseted status
	fireOnNetStreamStatus(RTMP::Netstream::Play::Reset,L"Playback reset");
	//Send play status
	fireOnNetStreamStatus(RTMP::Netstream::Play::Start,L"Playback started");

	//Add listener
	AddMediaListener(listener);
	//Attach
	Attach(sess);
}

/***************************************
 * Publish
 *	RTMP event listener
 **************************************/
void MediaBridgeSession::NetStream::doPublish(std::wstring& url)
{
	//Log
	Log("-Publish stream [%ls]\n",url.c_str());

	//Get participant stream
	if (!sess->ConsumeInputToken(url))
	{
		//Send error
		fireOnNetStreamStatus(RTMP::Netstream::Publish::BadName,L"Token invalid");
		//Exit
		return;
	}

	//Add this as listener
	AddMediaListener(sess);

	//Send publish notification
	fireOnNetStreamStatus(RTMP::Netstream::Publish::Start,L"Publish started");
	
}

/***************************************
 * Close
 *	RTMP event listener
 **************************************/
void MediaBridgeSession::NetStream::doClose(RTMPMediaStream::Listener *listener)
{
	//REmove listener
	RemoveMediaListener(listener);
	//Close
	Close();
}

void MediaBridgeSession::NetStream::Close()
{
	Log(">Close mediabridge netstream\n");

	//Dettach if playing
	Detach();

	Log("<Closed\n");
}

/********************************
 * NetConnection
 **********************************/
RTMPNetStream* MediaBridgeSession::CreateStream(DWORD streamId,DWORD audioCaps,DWORD videoCaps,RTMPNetStream::Listener *listener)
{
	//No stream for that url
	RTMPNetStream *stream = new NetStream(streamId,this,listener);

	//Register the sream
	RegisterStream(stream);

	//Create stream
	return stream;
}

void MediaBridgeSession::DeleteStream(RTMPNetStream *stream)
{
	//Unregister stream
	UnRegisterStream(stream);

	//Delete the stream
	delete(stream);
}


int  MediaBridgeSession::getQueueId()
{
	return queueId;
}
void  MediaBridgeSession::setQueueId(int p_queueId)
{
	this->queueId=p_queueId;
}

int  MediaBridgeSession::getSessionId()
{
	return sessionId;
}
void  MediaBridgeSession::setSessionId(int p_sessionId)
{
	this->sessionId=p_sessionId;
}

OnTextReceivedEvent::OnTextReceivedEvent(int p_session_id, std::wstring p_message)
{
	UTF8Parser utf8mess(p_message);
	int len = utf8mess.Serialize((BYTE*)message,PATH_MAX);
	message[len] = 0;
	//strncpy(message,p_message,PATH_MAX);
	session_id = p_session_id;
}

xmlrpc_value* OnTextReceivedEvent::GetXmlValue(xmlrpc_env *env)
{
	
	return xmlrpc_build_value(env,"(iis)",(int)MediaBridgeSession::OnTextReceivedEvent, session_id, message);
}

OnCmdReceivedEvent::OnCmdReceivedEvent(int p_session_id, MediaBridgeSession::TextCommands p_cmd)
{
	cmd = p_cmd;
	session_id = p_session_id;
}

xmlrpc_value* OnCmdReceivedEvent::GetXmlValue(xmlrpc_env *env)
{
	return xmlrpc_build_value(env,"(iii)",(int)MediaBridgeSession::OnCmdReceivedEvent, session_id, (int) cmd);
}

OnCSIANSIReceivedEvent::OnCSIANSIReceivedEvent(int p_session_id, std::wstring p_message)
{
	UTF8Parser utf8mess(p_message);
	int len = utf8mess.Serialize((BYTE*)CSIANSIMessage,PATH_MAX);
	CSIANSIMessage[len] = 0;
	session_id = p_session_id;
}

xmlrpc_value* OnCSIANSIReceivedEvent::GetXmlValue(xmlrpc_env *env)
{
	return xmlrpc_build_value(env,"(iis)",(int)MediaBridgeSession::OnCSIANSIReceivedEvent, session_id, CSIANSIMessage);
}

