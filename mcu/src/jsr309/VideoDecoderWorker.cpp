/* 
 * File:   VideoDecoderWorker.cpp
 * Author: Sergio
 * 
 * Created on 2 de noviembre de 2011, 23:38
 */

#include "VideoDecoderWorker.h"
#include "rtp.h"
#include "log.h"

VideoDecoderJoinableWorker::VideoDecoderJoinableWorker(bool useThread)
{
	//Nothing
	output = NULL;
	joined = NULL;
	decoding = false;
        this->useThread = useThread;
        videoDecoder = NULL;
}

VideoDecoderJoinableWorker::~VideoDecoderJoinableWorker()
{
	End();
}

int VideoDecoderJoinableWorker::Init(VideoOutput *output)
{
	//Store it
	this->output = output;
}

int VideoDecoderJoinableWorker::End()
{
	//Dettach
	Dettach();

	//Check if already decoding
	if (decoding)
		//Stop
		Stop();

	//Set null
	output = NULL;
}

int VideoDecoderJoinableWorker::Start()
{
	Log("-StartVideoDecoderJoinableWorker\n");

	//Check
	if (!output)
		//Exit
		return Error("null video output");

	//Check if need to restart
	if (decoding)
		//Stop first
		Stop();

        setZeroTime(&lastFPURequest);
	lostCount=0;
	frameSeqNum = RTPPacket::MaxExtSeqNum;
	lastSeq = RTPPacket::MaxExtSeqNum;
	waitIntra = false;

	//Start decoding
	decoding = 1;

	//launc thread
	if (useThread) createPriorityThread(&thread,startDecoding,this,0);

	return 1;
}
void * VideoDecoderJoinableWorker::startDecoding(void *par)
{
	Log("VideoDecoderJoinableWorkerThread [%d]\n",getpid());
	//Get worker
	VideoDecoderJoinableWorker *worker = (VideoDecoderJoinableWorker *)par;
	//Block all signals
	blocksignals();
	//Run
	worker->Decode();
	//Exit
	return NULL;
}


int  VideoDecoderJoinableWorker::Stop()
{
	Log(">StopVideoDecoderJoinableWorker\n");

	//If we were started
	if (decoding)
	{
		//Stop
		decoding=0;

		//Cancel any pending wait
                if (useThread)
                {
                    packets.Cancel();
                    pthread_join(thread,NULL);
                }
                else
                {
                    if (videoDecoder)
                    {
                        delete videoDecoder;
                        videoDecoder = NULL;
                    }
                }
	}

	Log("<StopVideoDecoderJoinableWorker\n");

	return 1;
}

void VideoDecoderJoinableWorker::DecodePacket(RTPPacket* packet)
{
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
                if (joined && getDifTime(&lastFPURequest)>1000000)
                {
                        //Debug
                        Log("-Requesting FPU lost %d\n",lostCount);
                        //Reset count
                        lostCount = 0;
                        //Request also over rtp
                        joined->Update();
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
                        return;
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
                        Error("Error creando nuevo decodificador de video [%p,%d]\n",this,type);
                        //Delete packet
                        if (useThread) delete(packet);
                        //Next
                        return;
                }
        }

        //Check if we have lost the last packet from the previous frame
        if (seq>frameSeqNum)
        {
                //Try to decode what is in the buffer
                videoDecoder->DecodePacket(NULL,0,1,1);
                //Get picture
                BYTE *frame = videoDecoder->GetFrame();
                DWORD width = videoDecoder->GetWidth();
                DWORD height = videoDecoder->GetHeight();
                //Check values
                if (frame && width && height)
                {
                        //Set frame size
                        output->SetVideoSize(width,height);
                        //Send it
                        output->NextFrame(frame);
                }
        }


        //Lo decodificamos
        if(!videoDecoder->DecodePacket(buffer,size,lost,packet->GetMark()))
        {
                //Check if we got listener and more than two seconds have elapsed from last request
                if (joined && getDifTime(&lastFPURequest)>1000000)
                {
                        //Debug
                        Log("-Requesting FPU decoder error\n");
                        //Reset count
                        lostCount = 0;
                        //Request also over rtp
                        joined->Update();
                        //Update time
                        getUpdDifTime(&lastFPURequest);
                        //Waiting for refresh
                        waitIntra = true;
                }
                //Delete packet
                delete(packet);
                //Next frame
                return;
        }

	//if ( (lastSeq % 20) == 0 ) Log("VideoDecoder: decoded frame codec %d.\n", videoDecoder->type );
        //Si es el ultimo
        if(packet->GetMark())
        {
                if (videoDecoder->IsKeyFrame())
                        Debug("-Got Intra\n");

                //No seq number for frame
                frameSeqNum = RTPPacket::MaxExtSeqNum;

                //Get picture
                BYTE *frame = videoDecoder->GetFrame();
                DWORD width = videoDecoder->GetWidth();
                DWORD height = videoDecoder->GetHeight();
                //Check values
                if (frame && width && height)
                {
                        //Set frame size
                        output->SetVideoSize(width,height);

                        //Send it
                        output->NextFrame(frame);
                }
                //Check if we got the waiting refresh
                if (waitIntra && videoDecoder->IsKeyFrame())
                        //Do not wait anymore
                        waitIntra = false;
        }

        //Delete packet
        if (useThread) delete(packet);
}

int VideoDecoderJoinableWorker::Decode()
{
	VideoDecoder*	videoDecoder = NULL;
	VideoCodec::Type type;

	Log(">DecodeVideo\n");

	//Mientras tengamos que capturar
	while(decoding)
	{
		//Obtenemos el paquete
		if (!packets.Wait(0))
			//Check condition again
			continue;

		//Get packet in queue
		RTPPacket* packet = packets.Pop();
		
		//Check
		if (!packet)
			//Check condition again
			continue;

                DecodePacket(packet);
	}

	//Borramos el encoder
	delete videoDecoder;
        videoDecoder = NULL;

	Log("<DecodeVideo\n");

	//Exit
	return 0;
}

void VideoDecoderJoinableWorker::onRTPPacket(RTPPacket &packet)
{
    //Put it on the queue
    if( decoding )
    {
        if( useThread )
            packets.Add( packet.Clone() );
        else
            DecodePacket( &packet );
    }
}

void VideoDecoderJoinableWorker::onResetStream()
{
	//Clean all packets
	packets.Clear();
}

void VideoDecoderJoinableWorker::onEndStream()
{
	//Stop decoding
	Stop();
	//Not joined anymore
	joined = NULL;
}

int VideoDecoderJoinableWorker::Attach(Joinable *join)
{
	//Detach if joined
	if (joined)
	{
		//Stop
		Stop();
		//Remove ourself as listeners
		joined->RemoveListener(this);
	}
	//Store new one
	joined = join;
	//If it is not null
	if (joined)
	{
		//Start
		Start();
		//Join to the new one
		join->AddListener(this);
	}
	//OK
	return 1;
}

int VideoDecoderJoinableWorker::Dettach()
{
        //Detach if joined
	if (joined)
	{
		//Stop decoding
		Stop();
		//Remove ourself as listeners
		joined->RemoveListener(this);
	}

	//Not joined anymore
	joined = NULL;
}
