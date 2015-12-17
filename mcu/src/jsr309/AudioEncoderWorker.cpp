/* 
 * File:   AudioEncoderWorker.cpp
 * Author: Sergio
 * 
 * Created on 4 de octubre de 2011, 20:42
 */

#include <set>
#include "log.h"
#include "AudioEncoderWorker.h"

AudioEncoderMultiplexerWorker::AudioEncoderMultiplexerWorker()
{
	//Nothing
	input = NULL;
	encoding = false;
	codec = (AudioCodec::Type)-1;
}

AudioEncoderMultiplexerWorker::~AudioEncoderMultiplexerWorker()
{
	End();
}

int AudioEncoderMultiplexerWorker::Init(AudioInput *input)
{
	//Store it
	this->input = input;
        return 1;
}
int AudioEncoderMultiplexerWorker::SetCodec(AudioCodec::Type codec)
{
	//Colocamos el tipo de audio
	this->codec = codec;
        

	//Check
	if (!listeners.empty() && !encoding)
        {
		//Start
		Start();
        }

	return 1;
}

int AudioEncoderMultiplexerWorker::Start()
{
	//Check
	if (!input)
		//Exit
		return Error("null audio input");

	//Check if need to restart
	if (encoding)
		//Stop first
		Stop();

	//Start decoding
	encoding = 1;

	//launc thread
	createPriorityThread(&thread,startEncoding,this,0);

	return 1;
}
void * AudioEncoderMultiplexerWorker::startEncoding(void *par)
{
	Log("AudioEncoderMultiplexerWorkerThread [%d]\n",getpid());
	//Get worker
	AudioEncoderMultiplexerWorker *worker = (AudioEncoderMultiplexerWorker *)par;
	//Block all signals
	blocksignals();
	//Run
	worker->Encode();
	//Exit
	return NULL;;
}

int AudioEncoderMultiplexerWorker::Stop()
{
	Log(">Stop AudioEncoderMultiplexerWorker\n");

	//If we were started
	if (encoding)
	{
		//Stop
		encoding=0;

		//Stop any pending grab
		input->CancelRecBuffer();

		//Esperamos
		pthread_join(thread,NULL);
	}

	Log("<Stop AudioEncoderMultiplexerWorker\n");

	return 1;
}

int AudioEncoderMultiplexerWorker::End()
{
	//Check if already decoding
	if (encoding)
		//Stop
		Stop();

	//Set null
	input = NULL;
}


/*******************************************
* SendAudio
*	Capturamos el audio y lo mandamos
*******************************************/
int AudioEncoderMultiplexerWorker::Encode()
{
	RTPPacket	packet(MediaFrame::Audio,codec,codec);
	SWORD 		recBuffer[512];
	AudioEncoder* 	encoder;
	DWORD		frameTime=0;

	Log(">Encode AudioEncoderMultiplexerWorker [%d,%s]\n",codec,AudioCodec::GetNameFor(codec));

	DWORD clock;
        DWORD rate;
	//Get ts multiplier
	float multiplier;

	//Mientras tengamos que capturar
	while(encoding)
	{
            if (encoder == NULL || encoder->type != codec)
            {
                if (encoder) delete encoder;

                encoder = AudioCodecFactory::CreateEncoder(codec);
                if (encoder == NULL)
                    return Error("Could not create codec\n");

                input->StartRecording(encoder->GetRate());
                Log("-JSR309 AudioEncoder: Started audio encoder %s at %d Hz.\n",
                    AudioCodec::GetNameFor(codec), encoder->GetRate());
                clock = encoder->GetClockRate();
                packet.SetClockRate(clock);
                
                rate = encoder->TrySetRate(input->GetNativeRate());
                multiplier = (float) clock/ (float) rate;
            }
		//Incrementamos el tiempo de envio
		frameTime += encoder->numFrameSamples*multiplier;

		//Capturamos
		if (input->RecBuffer(recBuffer,encoder->numFrameSamples)==0)
                {
                    msleep(1000);
                    continue;
                }


		//Lo codificamos
		int len = encoder->Encode(recBuffer,encoder->numFrameSamples,
                                          packet.GetMediaData(),packet.GetMaxMediaLength());

		//Comprobamos que ha sido correcto
		if(len<=0)
			continue;

		//Set frame time
		packet.SetTimestamp(frameTime);
		//Set length
		packet.SetMediaLength(len);
		
		//Multiplex it
		Multiplex(packet);
	}

	Log("-SendAudio cleanup[%d]\n",encoding);

	//Paramos de grabar por si acaso
	input->StopRecording();

	//Logeamos
	Log("-Deleting codec\n");

	//Borramos el codec
	delete encoder;

	//Salimos
        Log("<SendAudio\n");
}

void AudioEncoderMultiplexerWorker::AddListener(Listener *listener)
{
	//Check if we were already encoding
	if (listener && !encoding && codec!=-1)
		//Start encoding;
		Start();
	//Add the listener
	RTPMultiplexer::AddListener(listener);
}

void AudioEncoderMultiplexerWorker::RemoveListener(Listener *listener)
{
	//Remove the listener
	RTPMultiplexer::RemoveListener(listener);
	//If there are no more
	if (listeners.empty())
		//Stop encoding
		Stop();
}

void AudioEncoderMultiplexerWorker::Update()
{
}
