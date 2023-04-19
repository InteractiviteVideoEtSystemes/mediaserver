/* 
 * File:   VideoEncoderWorker.cpp
 * Author: Sergio
 * 
 * Created on 2 de noviembre de 2011, 23:37
 */

#include "VideoEncoderWorker.h"
#include "log.h"
#include "tools.h"
#include "RTPMultiplexer.h"
#include "acumulator.h"

VideoEncoderMultiplexerWorker::VideoEncoderMultiplexerWorker() : RTPMultiplexerSmoother()
{
	//Nothing
	input = NULL;
	encoding = false;
	sendFPU = false;
	codec = (VideoCodec::Type)-1;
        useInputSize = false;
	//Create objects
	pthread_mutex_init(&mutex,NULL);
	pthread_cond_init(&cond,NULL);
}

VideoEncoderMultiplexerWorker::~VideoEncoderMultiplexerWorker()
{
	End();
	//Clean object
	pthread_mutex_destroy(&mutex);
	pthread_cond_destroy(&cond);
}

int VideoEncoderMultiplexerWorker::Init(VideoInput *input)
{
	//Store it
	this->input = input;
}

int VideoEncoderMultiplexerWorker::SetCodec(VideoCodec::Type codec,int mode,int fps,int bitrate,int intraPeriod, Properties & properties)
{
	Log("-SetVideoCodec [%s=%d, fps:%d, bitrate:%d kbps, intra:%d]\n",VideoCodec::GetNameFor(codec),codec,fps,bitrate,intraPeriod);
	if (fps <= 0)
		return Error("Invalid value for fps:%d.\n", fps);

	//Store parameters
	this->codec	  = codec;
	this->mode	  = mode;
	this->bitrate	  = bitrate;
	this->fps	  = fps;
	this->intraPeriod = intraPeriod;
	//Init limits
	this->videoBitrateLimit		= bitrate;
	this->videoBitrateLimitCount	= fps;
	params = properties;

	Stop();

	//Get width and height
	width = GetWidth(mode);
	height = GetHeight(mode);

	//Check size
	if (!width || !height)
		//Error
		return Error("Unknown video mode\n");

	//Check if we are already encoding
	if (!listeners.empty())
	{
		//And start
		Log("-VideoEncoder: restarted encoder.\n");
		Start();
	}
	
	//Exit
	return 1;
}

int VideoEncoderMultiplexerWorker::Start()
{
	//Check
	if (!input)
		//Exit
		return Error("null video input");
	
	//Check if need to restart
	if (encoding)
		//Stop first
		Stop();

	//Start smoother
	RTPMultiplexerSmoother::Start();

	//Start decoding
	encoding = 1;

	//launc thread
	createPriorityThread(&thread,startEncoding,this,0);

	return 1;
}

void * VideoEncoderMultiplexerWorker::startEncoding(void *par)
{
	Log("VideoEncoderMultiplexerWorkerThread [%d]\n",getpid());
	//Get worker
	VideoEncoderMultiplexerWorker *worker = (VideoEncoderMultiplexerWorker *)par;
	//Block all signals
	blocksignals();
	//Run
	worker->Encode();
	//Exit
	return NULL;
}

int VideoEncoderMultiplexerWorker::Stop()
{
	Log(">Stop VideoEncoderMultiplexerWorker\n");

	//If we were started
	if (encoding)
	{
		//Stop
		encoding=0;

		//Cancel and frame grabbing
		input->CancelGrabFrame();

		//Cancel sending
		pthread_cond_signal(&cond);

		//Esperamos
		pthread_join(thread,NULL);
	}

	//Stop smoother
	RTPMultiplexerSmoother::Stop();

	Log("<Stop VideoEncoderMultiplexerWorker\n");

	return 1;
}

int VideoEncoderMultiplexerWorker::End()
{
	//Check if already decoding
	if (encoding)
		//Stop
		Stop();

	//Set null
	input = NULL;
}

void VideoEncoderMultiplexerWorker::AddListener(Listener *listener)
{
	//Check if we were already encoding
	if (listener && !encoding && codec != -1)
		//Start encoding;
		Start();
	//Add the listener
	RTPMultiplexer::AddListener(listener);
}

void VideoEncoderMultiplexerWorker::RemoveListener(Listener *listener)
{
	//Remove the listener
	RTPMultiplexer::RemoveListener(listener);
	//If there are no more
	if (listeners.empty())
		//Stop encoding
		Stop();
}

void VideoEncoderMultiplexerWorker::Update()
{
	//Sedn FPU
	sendFPU = true;
}

void VideoEncoderMultiplexerWorker::SetREMB(int estimation)
{
	//Set bitrate limit
	videoBitrateLimit = estimation/1000;
	//Set limit of bitrate to 1 second;
	videoBitrateLimitCount = fps;
}

int VideoEncoderMultiplexerWorker::Encode()
{
	timeval first;
	timeval prev;
	DWORD num = 0;
	QWORD overslept = 0;

	Acumulator bitrateAcu(1000);
	Acumulator fpsAcu(1000);
	VideoEncoder* videoEncoder = NULL;

	Log(">SendVideo [width:%d,size:%d,bitrate:%d,fps:%d,intra:%d]\n",width,height,bitrate,fps,intraPeriod);

	//Comrpobamos que tengamos video de entrada
	if (input == NULL)
		return Error("No video input");

	//Iniciamos el tama�o del video
	if (!input->StartVideoCapture(width,height,fps))
		return Error("Couldn't set video capture\n");

	//Start at 80%
	int current = bitrate;

	//No wait for first
	QWORD frameTime = 0;

	videoEncoder = VideoCodecFactory::CreateEncoder(codec, params);

	//Comprobamos que se haya creado correctamente
	if (videoEncoder == NULL)
	{
		//error
		Error("Can't create video encoder\n");
		encoding = false;
	}
	else
	{
		//Send at higher bitrate first frame, but skip frames after that so sending bitrate is kept
        //Disabled by IVES - to check later
		videoEncoder->SetFrameRate(fps,current,intraPeriod);
		//Iniciamos el tamama�o del encoder
 		videoEncoder->SetSize(width,height);
	
		Log("-Created %s video encoder.\n", VideoCodec::GetNameFor(codec));
	}

	//The time of the first one
	gettimeofday(&first,NULL);
	prev = first;

	//Started
	Log("-Sending video\n");

	//Mientras tengamos que capturar
	while (encoding)
	{
		//Nos quedamos con el puntero antes de que lo cambien
		BYTE *pic;

                if (useInputSize && input->HasNativeSizeChanged() )
                {
                    DWORD nativeWidth = input->GetNativeWidth();
                    DWORD nativeHeight = input->GetNativeHeight();

                    if ( nativeWidth > 0 && nativeHeight > 0
                            &&
                         (height != nativeHeight || width != nativeWidth ))
                    {
                        // If native size has changed, try to reconfigure the codec first
                        if ( videoEncoder->SetSize(nativeWidth,nativeHeight) )
                        {
                            // Ok - the codec accepted the change. Now change the capture ..
                            if ( input->StartVideoCapture(nativeWidth,nativeHeight,fps) )
                            {
                                width = nativeWidth;
                                height = nativeHeight;
                                Log("-VideoEncoder: adjusted to new native size %u x %u.\n", width, height);
                            }
                            else
                            {

                                // We could not start the capture - revert to previous codec settings
                                Error("-VideoEncoder: failed to restart capture with new size %d x %d .\n",
                                    nativeWidth, nativeHeight);
                                videoEncoder->SetSize(width,height);
                            }
                        }
                        else
                        {
                            // Ok - the codec did not accept the change - we keep the old picture
                            // @TODO: video encoder should return the "best supported format"
                             Error("-VideoEncoder: codec did not support size change. Keeping the old one.\n");
                        }
                    }
                }

                pic = input->GrabFrame(frameTime/1000);

		//Check picture
		if (!pic || codec == -1)
			//Exit
			continue;

		//Check if we need to send intra
		if (sendFPU)
		{
			//Log
			Log("-FastPictureUpdate\n");
			//Set it
			videoEncoder->FastPictureUpdate();
			//Do not send anymore
			sendFPU = false;
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
			else if (instant<bitrate)
				//Increase a 8% each second or fps kbps
				target += (DWORD)(target*0.08/fps)+1;
		}

		//Check target bitrate agains max conf bitrate
		if (target>bitrate*1.2)
			//Set limit to max bitrate allowing a 20% overflow so instant bitrate can get closer to target
			target = bitrate*1.2;

		//Check limits counter
		if (videoBitrateLimitCount>0)
			//One frame less of limit
			videoBitrateLimitCount--;

		//Check if we have a new bitrate
		if (target && target!=current)
		{
			//Reset bitrate
			videoEncoder->SetFrameRate(fps,target,intraPeriod);
			//Upate current
			current = target;
		}

		//Procesamos el frame
		VideoFrame *videoFrame = videoEncoder->EncodeFrame(pic,input->GetBufferSize());

		//If was failed
		if (!videoFrame)
			//Next
			continue;

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
			frameTime = 1000000/fps;
		} else {
			//Set frame time
			frameTime = 1000000/fps;
		}

		//Add frame size in bits to bitrate calculator
	        bitrateAcu.Update(getDifTime(&first)/1000,videoFrame->GetLength()*8);

		//Update fps count
		fpsAcu.Update(getDifTime(&first)/1000,1);

		//Set frame timestamp
		videoFrame->SetTimestamp(getDifTime(&first)/1000);

		//Set sending time of previous frame
		getUpdDifTime(&prev);
		
		//Calculate sending times based on bitrate
		DWORD sendingTime = videoFrame->GetLength()*8/current;

		//Adjust to maximum time
		if (sendingTime>frameTime/1000)
			//Cap it
			sendingTime = frameTime/1000;

		//Send it smoothly
		SmoothFrame(videoFrame,sendingTime);

		//if ((num%20) == 0)Log("-Send encoded frame codec %d.\n", videoEncoder->type);

		//Dump statistics
		if (num && ((num%fps*10)==0))
		{
			Debug("-Send bitrate current=%d avg=%llf rate=[%llf,%llf] fps=[%llf,%llf] limit=%d\n",
				current,bitrateAcu.GetInstantAvg()/1000,bitrateAcu.GetMinAvg()/1000,bitrateAcu.GetMaxAvg()/1000,
				fpsAcu.GetMinAvg(),fpsAcu.GetMaxAvg(),videoBitrateLimit);
			bitrateAcu.ResetMinMax();
			fpsAcu.ResetMinMax();
		}
		num++;
	}

	Log("-SendVideo out of loop\n");

	//Terminamos de capturar
	input->StopVideoCapture();

	//Check
	if (videoEncoder)
		//Borramos el encoder
		delete videoEncoder;

	//Salimos
	Log("<SendVideo [%d]\n",encoding);
}
