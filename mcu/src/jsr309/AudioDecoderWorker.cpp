/* 
 * File:   AudioDecoderWorker.cpp
 * Author: Sergio
 * 
 * Created on 4 de octubre de 2011, 20:06
 */
#include "log.h"
#include "AudioDecoderWorker.h"
#include "rtp.h"

AudioDecoderJoinableWorker::AudioDecoderJoinableWorker()
{
	//Nothing
	output = NULL;
        input = NULL;
	joined = NULL;
	decoding = false;
}

AudioDecoderJoinableWorker::~AudioDecoderJoinableWorker()
{
	End();
}

int AudioDecoderJoinableWorker::Init(AudioOutput *output)
{
	//Store it
	this->output = output;
        if (input)
        {
            input->StopRecording();
            input = NULL;
        }
        return 1;
}

int AudioDecoderJoinableWorker::Init(PipeAudioInput * input)
{
    if (input)
    {
        // We switch to use pipe audio input instead of audio output
        if (output)
        {
            output->StopPlaying();
            output = NULL;
        }
        //Store it
	this->input = input;
        return input->Init(16000);
    }
    else
    {
        // We do not tuch the output but we stop the previous pipe if needed
        if (this->input)
        {
            this->input->StopRecording();
            this->input = NULL;
        }
        return 1;
    }
}

int AudioDecoderJoinableWorker::End()
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

int AudioDecoderJoinableWorker::Start()
{
	Log("-StartAudioDecoderJoinableWorker\n");

	//Check
	if (!output && !input)
		//Exit
		return Error("null audio outputi/input not starting");

	//Check if need to restart
	if (decoding)
		//Stop first
		Stop();

	//Start decoding
	decoding = 1;

	//launc thread
	createPriorityThread(&thread,startDecoding,this,0);

	return 1;
}
void * AudioDecoderJoinableWorker::startDecoding(void *par)
{
	Log("AudioDecoderJoinableWorkerThread [%d]\n",getpid());
	//Get worker
	AudioDecoderJoinableWorker *worker = (AudioDecoderJoinableWorker *)par;
	//Block all signals
	blocksignals();
	//Run
	worker->Decode();
	//Exit
	return NULL;;
}

int  AudioDecoderJoinableWorker::Stop()
{
	Log(">StopAudioDecoderJoinableWorker\n");

	//If we were started
	if (decoding)
	{
		//Stop
		decoding=0;

		//Cancel any pending wait
		packets.Cancel();

		//Esperamos
		pthread_join(thread,NULL);
	}

	Log("<StopAudioDecoderJoinableWorker\n");

	return 1;
}


int AudioDecoderJoinableWorker::Decode()
{
	SWORD		raw[512];
	DWORD		rawSize=512;
	AudioDecoder*	codec=NULL;
	DWORD		frameTime=0;
	DWORD		lastTime=0;

	Log(">JSR309 DecodeAudio\n");


	//Mientras tengamos que capturar
	while(decoding)
	{
		//Obtenemos el paquete
		if (!packets.Wait(0))
                {
			//Check condition again
                    msleep(1000);
			continue;
                }

		//Get packet in queue
		RTPPacket* packet = packets.Pop();
		
		//Check
		if (!packet)
                {
                    msleep(1000);
                    continue;
                }

		//Comprobamos el tipo
		if ( codec==NULL  || packet->GetCodec()!=codec->type )
		{
			//Si habia uno nos lo cargamos
			if (codec!=NULL)
                        {
                            if (input) input->StopRecording();
                            if (output) output->StopPlaying();
			    delete codec;
                        }

			//Creamos uno dependiendo del tipo
                        codec = AudioCodecFactory::CreateDecoder((AudioCodec::Type)packet->GetCodec());
			if ( codec != NULL )
                        {
                            DWORD rate;

                            if (output)
                            {
                                rate = output->GetNativeRate();
                            }
                            else
                            {
                                rate = 16000;
                            }

                            rate = codec->TrySetRate(rate);
                            if (input)
                            {

                                input->Init(rate);
                            }

                            if (output)
                            {
                                output->StartPlaying(rate);
                            }
                        }
                        else
                        {
                            Error("Failed to open audio decoder\n");
                            break;
                        }
			
		}

		//Lo decodificamos
		int len = codec->Decode(packet->GetMediaData(),packet->GetMediaLength(),raw,rawSize);

		//Obtenemos el tiempo del frame
		frameTime = packet->GetTimestamp() - lastTime;

		//Actualizamos el ultimo envio
		lastTime = packet->GetTimestamp();

		//Y lo reproducimos
		if (output != NULL) output->PlayBuffer(raw,len,frameTime);
                if (input != NULL) input->PutSamples(raw, len);

		//Delete packet
		delete(packet);
	}

	//End reproducing
	if (output != NULL) output->StopPlaying();
        if (input != NULL) input->End();

	//If a decoder is created, delete it
	if (codec!=NULL) delete codec;		
	
	Log("<DecodeAudio\n");
}

void AudioDecoderJoinableWorker::onRTPPacket(RTPPacket &packet)
{
	//Put it on the queue
	packets.Add(packet.Clone());
}

void AudioDecoderJoinableWorker::onResetStream()
{
	//Clean all packets
	packets.Clear();
}

void AudioDecoderJoinableWorker::onEndStream()
{
	//Stop decoding
	Stop();
	//Not joined anymore
	joined = NULL;
}

int AudioDecoderJoinableWorker::Attach(Joinable *join)
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

int AudioDecoderJoinableWorker::Dettach()
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
