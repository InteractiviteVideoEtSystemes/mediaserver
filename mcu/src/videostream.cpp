#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>
#include <math.h>
#include "videostream.h"
#include "h263/h263codec.h"
#include "h263/mpeg4codec.h"
#include "h264/h264encoder.h"
#include "h264/h264decoder.h"
#include "log.h"
#include "tools.h"
#include "acumulator.h"
#include "RTPSmoother.h"


/**********************************
* VideoStream
*	Constructor
***********************************/
VideoStream::VideoStream( Listener *l, Logo &muteLogo, MediaFrame::MediaRole role ) :
    rtp( MediaFrame::Video, l, role ), logo( muteLogo )
{
    //Inicializamos a cero todo
    sendingVideo = TaskIdle;
    receivingVideo = TaskIdle;
    videoInput = NULL;
    videoOutput = NULL;
    rtpSession = NULL;

    videoCodec = VideoCodec::H263_1996;
    videoCaptureMode = 0;
    videoGrabWidth = 0;
    videoGrabHeight = 0;
    videoFPS = 0;
    videoBitrate = 0;
    videoIntraPeriod = 0;
    videoBitrateLimit = 0;
    videoBitrateLimitCount = 0;
    sendFPU = false;
    listener = l;
    mediaListener = NULL;
    muted = false;
    mediaRole = role;

    recSSRC = 0;

    //Create objects
    pthread_mutex_init( &mutex, NULL );
    pthread_cond_init( &cond, NULL );
}

/*******************************
* ~VideoStream
*	Destructor. Cierra los dispositivos
********************************/
VideoStream::~VideoStream()
{
	//Clean object
	pthread_mutex_destroy(&mutex);
	pthread_cond_destroy(&cond);

}

/**********************************************
* SetVideoCodec
*	Fija el modo de envio de video 
**********************************************/
int VideoStream::SetVideoCodec(VideoCodec::Type codec,int mode,int fps,int bitrate,int intraPeriod,const Properties& properties)
{
	Log("-SetVideoCodec [%s,%dfps,%dkbps,intra:%d]\n",VideoCodec::GetNameFor(codec),fps,bitrate,intraPeriod);

	//LO guardamos
	videoCodec=codec;

	//Guardamos el bitrate
	videoBitrate=bitrate;
	
	//Store properties
	videoProperties = properties;
	
	if (mediaRole == MediaFrame::VIDEO_SLIDES)
	{
		videoProperties.SetProperty("h264.qpel", "6");
	}
	
	//The intra period
	if (intraPeriod>0)
		videoIntraPeriod = intraPeriod;

	//Get width and height
	videoGrabWidth = GetWidth(mode);
	videoGrabHeight = GetHeight(mode);

	//Check size
	if (!videoGrabWidth || !videoGrabHeight)
		//Error
		return Error("Unknown video mode\n");

	//Almacenamos el modo de captura
	videoCaptureMode=mode;

	//Y los fps
	videoFPS=fps;

	return 1;
}

int VideoStream::SetTemporalBitrateLimit(int estimation)
{




	//Set bitrate limit
	videoBitrateLimit = estimation/1000;
	//Set limit of bitrate to 1 second;
	videoBitrateLimitCount = videoFPS;
	//Exit
	return 1;
}

void VideoStream::SetRemoteRateEstimator(RemoteRateEstimator* estimator)
{
	//Set it in the rtp session
	rtp.SetRemoteRateEstimator(estimator);
}

/***************************************
* Init
*	Inicializa los devices 
***************************************/
int VideoStream::Init(VideoInput *input,VideoOutput *output)
{
	Log(">Init video stream\n");
	
	//Guardamos los objetos
	if (input != NULL)
		videoInput  = input;
	if (output != NULL)
		videoOutput = output;
	
	//No estamos haciendo nada
	sendingVideo = TaskIdle;
	receivingVideo = TaskIdle;
	
	//Iniciamos el rtp
	if(!rtp.Init())
		return Error("No hemos podido abrir el rtp\n");

	//Init smoother
	smoother.Init(&rtp);

	Log("<Init video stream\n");

	return 1;
}

int VideoStream::SetLocalCryptoSDES(const char* suite, const char* key64)
{
	return rtp.SetLocalCryptoSDES(suite,key64);
}

int VideoStream::SetRemoteCryptoSDES(const char* suite, const char* key64, int keyRank)
{
	
	return rtp.SetRemoteCryptoSDES(suite,key64,keyRank);
}

int VideoStream::SetRemoteCryptoDTLS(const char *setup,const char *hash,const char *fingerprint)
{
	return rtp.SetRemoteCryptoDTLS(setup, hash, fingerprint);
}

int VideoStream::SetLocalSTUNCredentials(const char* username, const char* pwd)
{
	return rtp.SetLocalSTUNCredentials(username,pwd);
}

int VideoStream::SetRemoteSTUNCredentials(const char* username, const char* pwd)
{
	return rtp.SetRemoteSTUNCredentials(username,pwd);
}
int VideoStream::SetRTPProperties(const Properties& properties)
{
	//For each property
	for (Properties::const_iterator it=properties.begin();it!=properties.end();++it)
	{
		if (it->first.compare(0, 6, "codec.")==0)
		{
			std::string key = it->first.substr(6, std::string::npos);
			videoProperties[key] = it->second; 
		}
	}
	return rtp.SetProperties(properties);
}
/**************************************
* startSendingVideo
*	Function helper for thread
**************************************/
void* VideoStream::startSendingVideo(void *par)
{
	Log("SendVideoThread [%d]\n",getpid());

	//OBtenemos el objeto
	VideoStream *conf = (VideoStream *)par;

	//Bloqueamos las se�ales
	blocksignals();

	//Y ejecutamos la funcion
	pthread_exit((void *)conf->SendVideo());
}

/**************************************
* startReceivingVideo
*	Function helper for thread
**************************************/
void* VideoStream::startReceivingVideo(void *par)
{
	Log("RecVideoThread [%d]\n",getpid());

	//Obtenemos el objeto
	VideoStream *conf = (VideoStream *)par;

	//Bloqueamos las se�a�es
	blocksignals();

	//Y ejecutamos
	pthread_exit( (void *)conf->RecVideo());
}

/***************************************
* StartSending
*	Comienza a mandar a la ip y puertos especificados
***************************************/
int VideoStream::StartSending(char *sendVideoIp,int sendVideoPort,RTPMap& rtpMap)
{
	Log(">StartSendingVideo [%s,%d]\n",sendVideoIp,sendVideoPort);

	//Y esperamos que salga
	StopSending();

    if (sendingVideo != TaskIdle)
		return Error("Cannot start sending video: bad state.\n");

	//Si tenemos video
	if (sendVideoPort==0)
		return Error("No video\n");

	//Iniciamos las sesiones rtp de envio
	if(!rtp.SetRemotePort(sendVideoIp,sendVideoPort))
		return Error("Error abriendo puerto rtp\n");

	//Set sending map
	rtp.SetSendingRTPMap(rtpMap);
	
	//Set video codec
	if(!rtp.SetSendingCodec(videoCodec))
		//Error
		return Error("%s video codec not supported by peer\n",VideoCodec::GetNameFor(videoCodec));

	//Estamos mandando
	sendingVideo = TaskStarting;

	//Arrancamos los procesos
	createPriorityThread(&sendVideoThread,startSendingVideo,this,0);

	//LOgeamos
	Log("<StartSending video in thread [%d]\n",sendVideoThread);

	return 1;
}

int VideoStream::StartSending()
{
	Log(">StartSendingVideo with previous configuration\n");

	//Si estabamos mandando tenemos que parar
	StopSending();

	int sendVideoPort = rtp.GetRemotePort();

	//Si tenemos video
	if (sendVideoPort==0)
		return Error("No video\n");
	
        if (sendingVideo != TaskIdle)
		return Error("Cannot start sending video: bad state.\n");
	
	//Estamos mandando
	sendingVideo = TaskStarting;

	//Arrancamos los procesos
	createPriorityThread(&sendVideoThread,startSendingVideo,this,0);

	//LOgeamos
	Log("<StartSending video [%d]\n",sendingVideo);

	return 1;
}

/***************************************
* StartReceiving
*	Abre los sockets y empieza la recetpcion
****************************************/
int VideoStream::StartReceiving(RTPMap& rtpMap)
{
	//Si estabamos reciviendo tenemos que parar
	StopReceiving();	

	if (receivingVideo != TaskIdle)
		return Error("Failed to start receiving video. Task in bad state.\n");

	//Iniciamos las sesiones rtp de recepcion
	int recVideoPort= rtp.GetLocalPort();

	//Set receving map
	rtp.SetReceivingRTPMap(rtpMap);

	//Estamos recibiendo
	receivingVideo= TaskStarting;

	//Arrancamos los procesos
	createPriorityThread(&recVideoThread,startReceivingVideo,this,0);

	//Logeamos
	Log("-StartReceiving Video [%d]\n",recVideoPort);

	return recVideoPort;
}

int VideoStream::StartReceiving()
{
	//Si estabamos reciviendo tenemos que parar
	StopReceiving();	

	if (receivingVideo != TaskIdle)
		return Error("Failed to start receiving video. Task in bad state.\n");

	//Iniciamos las sesiones rtp de recepcion
	int recVideoPort= rtp.GetLocalPort();

	//Estamos recibiendo
	receivingVideo = TaskStarting;

	//Arrancamos los procesos
	createPriorityThread(&recVideoThread,startReceivingVideo,this,0);

	//Logeamos
	Log("-StartReceiving Video [%d]\n",recVideoPort);

	return recVideoPort;
}


/***************************************
* End
*	Termina la conferencia activa
***************************************/
int VideoStream::End()
{
	int ret;
	Log(">End\n");

	//Close smoother
	smoother.End();

	//Cerramos la session de rtp
	rtp.End();
	
	//Cerramos la session de rtp
	if (rtpSession)
		rtpSession->End();


	ret = StopReceiving();
	ret &= StopSending();

	rtpSession = NULL;
	
	
	Log("<End\n");

	return ret;
}

/***************************************
* StopSending
*     Manu paranood version.
***************************************/
int VideoStream::StopSending()
{
	// save thread ID
	pthread_t localsendVideoThread = sendVideoThread;
	
	Log(">StopSending thread=[%d]\n",localsendVideoThread);

	//Esperamos a que se cierren las threads de envio
	if (sendingVideo == TaskRunning || sendingVideo == TaskStarting)
	{
	    for (int i = 0; i < 10; i++)
	    {
		//Paramos el envio
		sendingVideo = TaskStopping;

		//Check we have video
		if (videoInput)
			//Cencel video grab
			videoInput->CancelGrabFrame();

		//Cancel sending
		pthread_cond_signal(&cond);
		msleep(100000);
		
		if ( sendingVideo == TaskIdle )
		{
		    //Y esperamos
		    pthread_join(localsendVideoThread,NULL);
		    Log("<StopSending thread=[%lu] after %d attempt(s).\n", localsendVideoThread, i);
		    return 1;
		}
	     }
	     return Error("<Failed to stop thread=[%lu]. something is blocked. running thread=[%lu]\n",
	                   localsendVideoThread, sendVideoThread);
	}

	Log("<StopSending\n");

	return 1;
}

/***************************************
* StopReceiving
*	Termina la recepcion
***************************************/
int VideoStream::StopReceiving()
{
	pthread_t localrecVideoThread = recVideoThread;
	Log(">StopReceiving thread ID=%d\n", localrecVideoThread);
	
	//Y esperamos a que se cierren las threads de recepcion
	if (receivingVideo == TaskRunning || receivingVideo == TaskStarting)
	{
	    for (int i = 0; i < 10; i++)
	    {

		//Dejamos de recivir
		receivingVideo = TaskStopping;
		
		if (rtpSession)
			//Cancel rtp
			rtpSession->CancelGetPacket(recSSRC);
		
		msleep(100000);
		if (receivingVideo == TaskIdle)
		{
		    //Esperamos
		     pthread_join(localrecVideoThread,NULL);
		     Log("<StopReceiving\n");
		     return 1;
		}
	    }
	}

	Log("<StopReceiving\n");

	return 1;
}

/*******************************************
* SendVideo
*	Capturamos el video y lo mandamos
*******************************************/
int VideoStream::SendVideo()
{
	timeval first;
	timeval prev;
	timeval lastFPU;
	timeval statstimer;
	
	DWORD num = 0;
	QWORD overslept = 0;

	Acumulator bitrateAcu(1000);
	DWORD fpsOut = 0;
	
	Log(">SendVideo [width:%d,size:%d,bitrate:%d,fps:%d,intra:%d]\n",videoGrabWidth,videoGrabHeight,videoBitrate,videoFPS,videoIntraPeriod);

	//Creamos el encoder
	VideoEncoder* videoEncoder = VideoCodecFactory::CreateEncoder(videoCodec,videoProperties);

	//Comprobamos que se haya creado correctamente
	if (videoEncoder == NULL)
		//error
		return Error("Can't create video encoder\n");

	//Comrpobamos que tengamos video de entrada
	if (videoInput == NULL)
		return Error("No video input");

	//Iniciamos el tama�o del video
	if (!videoInput->StartVideoCapture(videoGrabWidth,videoGrabHeight,videoFPS))
		return Error("Couldn't set video capture\n");

	//Start at 80%
	int current = videoBitrate*0.8;

	//Send at higher bitrate first frame, but skip frames after that so sending bitrate is kept
	videoEncoder->SetFrameRate(videoFPS,current*5,videoIntraPeriod);

	//No wait for first
	QWORD frameTime = 0;

	//Iniciamos el tamama�o del encoder
 	videoEncoder->SetSize(videoGrabWidth,videoGrabHeight);

	//The time of the first one
	gettimeofday(&first,NULL);
	gettimeofday(&statstimer,NULL);

	//The time of the previos one
	gettimeofday(&prev,NULL);

	//Fist FPU
	gettimeofday(&lastFPU,NULL);
	
	//Started
	Log("-Sending video\n");

	// Mark task as running

	int intputErrCount = 0;
	if ( sendingVideo == TaskStarting) sendingVideo = TaskRunning;
	//Mientras tengamos que capturar
	while (sendingVideo == TaskRunning)
	{
		//Nos quedamos con el puntero antes de que lo cambien
		BYTE *pic = videoInput->GrabFrame(frameTime/1000);

		//Check picture
		if (!pic)
		{
            msleep(1000);
			if ( intputErrCount++ > 10 )
			{
			    // If too many errors -> videoInput has been closed
			    Log("-videostream: stop sending video because input is not active.\n");
			    sendingVideo = TaskStopping;
			    break;
			}
			else
			{
				continue;
			}
		}

		intputErrCount = 0;
		//Check if we need to send intra
		if (sendFPU)
		{
			//Do not send anymore
			sendFPU = false;
			//Do not send if just send one (100ms)
			if( getDifTime( &lastFPU ) / 1000 > 100 )
			{
				//Set it
				videoEncoder->FastPictureUpdate();
				//Update last FPU
				getUpdDifTime(&lastFPU);
			}
		}

		//Calculate target bitrate
		int target = current;

		//Check temporal limits for estimations
		if (bitrateAcu.IsInWindow())
		{
			//Get real sent bitrate during last second and convert to kbits 
			DWORD instant = bitrateAcu.GetInstantAvg()/1000;
			//If we are in quarentine
			if (videoBitrateLimitCount)
				//Limit sending bitrate
				target = videoBitrateLimit;
			//Check if sending below limits
			else if (instant<videoBitrate)
				//Increase a 8% each second or fps kbps
				target += (DWORD)(target*0.08/videoFPS)+1;
		}

		//Check target bitrate agains max conf bitrate
		if (target>videoBitrate*1.2)
			//Set limit to max bitrate allowing a 20% overflow so instant bitrate can get closer to target
			target = videoBitrate*1.2;

		//Check limits counter
		if (videoBitrateLimitCount>0)
			//One frame less of limit
			videoBitrateLimitCount--;

		//Check if we have a new bitrate
		if (target && target!=current)
		{
			//Reset bitrate
			videoEncoder->SetFrameRate(videoFPS,target,videoIntraPeriod);
			//Upate current
			current = target;
		}
		
		//Procesamos el frame
		VideoFrame *videoFrame = videoEncoder->EncodeFrame(pic,videoInput->GetBufferSize());

		//If was failed
		if (!videoFrame)
			//Next
			continue;

		//Increase frame counter
		fpsOut++;
		
		//Check
		if (frameTime)
		{
			timespec ts;
			//Lock
			pthread_mutex_lock(&mutex);
			//Calculate slept time
			QWORD sleep = frameTime;
			//Remove extra sleep from prev
			if (overslept<sleep)
				//Remove it
				sleep -= overslept;
			else
				//Do not overflow
				sleep = 1;
			//Calculate timeout
			calcAbsTimeoutNS(&ts,&prev,sleep);
			//Wait next or stopped
			int canceled  = !pthread_cond_timedwait(&cond,&mutex,&ts);
			//Unlock
			pthread_mutex_unlock(&mutex);
			//Check if we have been canceled
			if (canceled)
				//Exit
				break;
			//Get differencence
			QWORD diff = getDifTime(&prev);
			//If it is biffer
			if (diff>frameTime)
				//Get what we have slept more
				overslept = diff-frameTime;
			else
				//No oversletp (shoulddn't be possible)
				overslept = 0;
		}

		//If first
		if (!frameTime)
		{
			//Set frame time, slower
			frameTime = 5*1000000/videoFPS;
			//Restore bitrate
			videoEncoder->SetFrameRate(videoFPS,current,videoIntraPeriod);
		} else {
			//Set frame time
			frameTime = 1000000/videoFPS;
		}

		//Add frame size in bits to bitrate calculator
		bitrateAcu.Update(getDifTime(&first)/1000,videoFrame->GetLength()*8);

		//Set frame timestamp
		videoFrame->SetTimestamp(getDifTime(&first)/1000);

		//Check if we have mediaListener
		if (mediaListener)
			//Call it
			mediaListener->onMediaFrame(*videoFrame);

		//Set sending time of previous frame
		getUpdDifTime(&prev);

		//Calculate sending times based on bitrate
		DWORD sendingTime = videoFrame->GetLength()*8/current;

		//Adjust to maximum time
		if (sendingTime>frameTime/1000)
			//Cap it
			sendingTime = frameTime/1000;

		//Send it smoothly
		smoother.SendFrame(videoFrame,sendingTime);

		//Dump statistics
		DWORD statstime2 = (DWORD) (getDifTime(&statstimer) / 1000);
		if ( statstime2 >= 20000)
		{
			Log("-Send video stats for participant codec = %s.\n", VideoCodec::GetNameFor(videoCodec));
			Log("                  current bitrate=%d kbit/s  avg=%8.2f kbit/s  limit=%d kbit/s\n",
                            current,bitrateAcu.GetInstantAvg()/1000,videoBitrateLimit);
			Log("                  fps=[%d]\n",
                            (fpsOut*1000)/statstime2);
			bitrateAcu.ResetMinMax();
			getUpdDifTime(&statstimer);
			fpsOut = 0;
		}
		num++;
	}

	Log("-SendVideo out of loop\n");
	sendingVideo = TaskIdle;
	//Terminamos de capturar
	videoInput->StopVideoCapture();

	//Check
	if (videoEncoder)
		//Borramos el encoder
		delete videoEncoder;

	//Salimos
	Log("<SendVideo [%d]\n",sendingVideo);

	return 0;
}

/****************************************
* RecVideo
*	Obtiene los packetes y los muestra
*****************************************/
int VideoStream::RecVideo()
{
	VideoDecoder*	videoDecoder = NULL;
	VideoCodec::Type type;
	timeval 	before;
	timeval		lastFPURequest;
	DWORD		lostCount=0, width=0, height=0;
	DWORD		frameSeqNum = RTPPacket::MaxExtSeqNum;
	DWORD		lastSeq = RTPPacket::MaxExtSeqNum;
	bool		waitIntra = false;
	Log(">RecVideo\n");
	
	if (rtpSession == NULL)
		rtpSession = &rtp;
	
	//Inicializamos el tiempo
	gettimeofday(&before,NULL);

	//Not sent FPU yet
	setZeroTime(&lastFPURequest);

	//Mientras tengamos que capturar
	rtpSession->ResetPacket(recSSRC, false);
	if ( receivingVideo == TaskStarting) receivingVideo = TaskRunning;

	while (receivingVideo == TaskRunning)
	{
		//Obtenemos el paquete
		RTPPacket* packet = rtpSession->GetPacket(recSSRC);

		//Check
		if (!packet)
                {
			//Next
                    msleep(1000);
		    continue;
                }

		//Get extended sequence number
		DWORD seq = packet->GetExtSeqNum();

		//Get packet data
		BYTE* buffer = packet->GetMediaData();
		DWORD size = packet->GetMediaLength();

		//Get type
		type = (VideoCodec::Type)packet->GetCodec();

		//Lost packets since last
		DWORD lost = 0;

		//If not first
		if (lastSeq!=RTPPacket::MaxExtSeqNum)
			//Calculate losts
			lost = seq-lastSeq-1;

		//Increase total lost count
		lostCount += lost;

		//Update last sequence number
		lastSeq = seq;

		//Si hemos perdido un paquete or still have not got an iframe
		if(lostCount>1 || waitIntra)
		{
			//Check if we got listener and more than two seconds have elapsed from last request
			if( listener && getDifTime( &lastFPURequest ) > 10000000 )
			{
				//Debug
				Log("-Requesting FPU lost %d ssrc= %08x\n",lostCount,recSSRC);
				//Reset count
				lostCount = 0;
				//Request it
				listener->onRequestFPU();
				//Request also over rtp
				rtpSession->RequestFPU(recSSRC);
				//Update time
				getUpdDifTime(&lastFPURequest);
				//Waiting for refresh
				waitIntra = true;
			}
		}

		//Check if it is a redundant packet
		if (type==VideoCodec::RED)
		{
			//Get redundant packet
			RTPRedundantPacket* red = (RTPRedundantPacket*)packet;
			//Get primary codec
			type = (VideoCodec::Type)red->GetPrimaryCodec();
			//Check it is not ULPFEC redundant packet
			if (type==VideoCodec::ULPFEC)
			{
				//Delete packet
				delete(packet);
				//Skip
				continue;
			}
			//Update primary redundant payload
			buffer = red->GetPrimaryPayloadData();
			size = red->GetPrimaryPayloadSize();
		}
		
		//Comprobamos el tipo
		if ((videoDecoder==NULL) || (type!=videoDecoder->type))
		{
			//Si habia uno nos lo cargamos
			if (videoDecoder!=NULL)
				delete videoDecoder;

			//Creamos uno dependiendo del tipo
			videoDecoder = VideoCodecFactory::CreateDecoder(type);

			//Si es nulo
			if (videoDecoder==NULL)
			{
				Error("Error creando nuevo decodificador de video [%d]\n",type);
				//Delete packet
				delete(packet);
				//Next
				continue;
			}
		}

		//Check if we have lost the last packet from the previous frame
		if (seq>frameSeqNum)
		{
			//Try to decode what is in the buffer
			videoDecoder->DecodePacket(NULL,0,1,1);
			//Get picture
			BYTE *frame = videoDecoder->GetFrame();
			width = videoDecoder->GetWidth();
			height = videoDecoder->GetHeight();
			//Check values
			if (frame && width && height)
			{
				//Set frame size
				if (videoOutput != NULL)
					videoOutput->SetVideoSize(width,height);

				//Check if muted
				if (!muted)
					//Send it
				if (videoOutput != NULL)
				{
					videoOutput->NextFrame(frame);
				}
			}
		}

		
		//Lo decodificamos
		if(!videoDecoder->DecodePacket(buffer,size,lost,packet->GetMark()))
		{
			//Check if we got listener and more than two seconds have elapsed from last request
			if (listener && getDifTime(&lastFPURequest)>1000000)
			{
				//Debug
				Log("-Requesting FPU decoder error\n");
				//Reset count
				lostCount = 0;
				//Request it
				listener->onRequestFPU();
				//Request also over rtp
				rtpSession->RequestFPU(recSSRC);
				//Update time
				getUpdDifTime(&lastFPURequest);
				//Waiting for refresh
				waitIntra = true;
			}
			//Delete packet
			delete(packet);
			//Next frame
			continue;
		}

						
		//Si es el ultimo
		if(packet->GetMark())
		{
			if (videoDecoder->IsKeyFrame())
				Log("-Got Intra\n");
			
			//No seq number for frame
			frameSeqNum = RTPPacket::MaxExtSeqNum;

			//Get picture
			BYTE *frame = videoDecoder->GetFrame();
			//DWORD width = videoDecoder->GetWidth();
			//If it is muted
			if (muted)
			{
				frame = logo.GetFrame();
				//Check size
				if (frame && (logo.GetWidth()!=width || logo.GetHeight()!=height))
				{
					//Get dimension
					width = logo.GetWidth();
					height = logo.GetHeight();
				if (videoOutput != NULL)
					//Set them in the encoder
					videoOutput->SetVideoSize(width,height);
				}			
				waitIntra = false;
			}
			else
			{
				frame = videoDecoder->GetFrame();
				//Check size
				if (frame /*&& (videoDecoder->GetWidth()!=width || videoDecoder->GetHeight()!=height)*/)
				{
					//Get dimension
					width = videoDecoder->GetWidth();
					height = videoDecoder->GetHeight();
				if (videoOutput != NULL)
					//Set them in the encoder
					videoOutput->SetVideoSize(width,height);
				}
				
				//Check if we got the waiting refresh
				if (waitIntra && videoDecoder->IsKeyFrame())
					//Do not wait anymore
					waitIntra = false;

			} 
			if (frame )
			{
			
				//Send
				if (videoOutput != NULL ) videoOutput->NextFrame(frame);
			}
		
		}
		//Delete packet
		delete(packet);
	}

	//Borramos el encoder
	delete videoDecoder;
	receivingVideo = TaskIdle;

	Log("<RecVideo\n");

	//Salimos
	pthread_exit(0);
}

int VideoStream::SetMediaListener( MediaFrame::Listener *l )
{
	//Set it
	mediaListener = l;
}

int VideoStream::SendFPU()
{
	//Next shall be an intra
	sendFPU = true;
	
	return 1;
}

MediaStatistics VideoStream::GetStatistics()
{
	MediaStatistics stats;

	if (rtpSession)
	{
            rtpSession->GetStatistics(recSSRC, stats);
	}
	//Fill stats
	stats.isReceiving	= IsReceiving();
	stats.isSending		= IsSending();

	//Return it
	return stats;
}

int VideoStream::SetMute(bool isMuted)
{
	//Set it
	muted = isMuted;
	if (muted)
	{
		//Push the avatar logo
		BYTE *frame 	= logo.GetFrame();
		DWORD width		= logo.GetWidth();
		DWORD height	= logo.GetHeight();
		//Check size
		if (frame && videoOutput != NULL)
		{
			//Set them in the encoder
			videoOutput->SetVideoSize(width,height);
			videoOutput->NextFrame(frame);
		}
	}
	if (videoOutput != NULL)
		videoOutput->KeepAspectRatio(!isMuted);
	//Exit
	return 1;
}
