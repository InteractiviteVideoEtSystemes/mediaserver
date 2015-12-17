/* 
 * File:   AudioTranscoder.cpp
 * Author: ebuu
 * 
 * Created on 7 août 2014, 00:07
 */

#include "AudioTranscoder.h"

AudioTranscoder::AudioTranscoder(const std::wstring & name) : tag(name)
{
    state = 0;
    recCodec = -1;
}

AudioTranscoder::~AudioTranscoder()
{
	End();
}

int AudioTranscoder::Init(bool allowBriding)
{
    int ret = 1;
    this->allowBridging = allowBriding;
    ret = encoder.Init(&pipe);
    if (ret > 0)
    {
        // This will init the pipe too
        ret = decoder.Init(&pipe);
        if ( !ret )
        {
            Error("-JSR309 AudioTranscoder: failed to init audio decoder.");
            encoder.End();
        }
        if (allowBriding)
            state = 0; // Probing
        else
            state = 2; // Transcoding
    }
    else
        Error("-JSR309 AudioTranscoder: failed to init audio encoder.");


    return ret;
}

int AudioTranscoder::End()
{
    decoder.End();
    pipe.End();
    encoder.End();
    return 1;
}
             
void AudioTranscoder::AddListener(Joinable::Listener *listener)
{
	encoder.AddListener(listener);
}

void AudioTranscoder::Update()
{
    //Do nothing - update not relevant for audio
}

void AudioTranscoder::SetREMB(DWORD estimation)
{
    //encoder.SetREMB(estimation);
}

void AudioTranscoder::RemoveListener(Joinable::Listener *listener)
{
	encoder.RemoveListener(listener);
}

void AudioTranscoder::onRTPPacket(RTPPacket &packet)
{
    if (allowBridging)
    {
        if ( recCodec != packet.GetCodec() || state == 0)
        {
            int ret = encoder.TryCodec(packet.GetCodec());

            if ( ret == packet.GetCodec() )
            {
                // endpoints support codes - no neet to transcode
                state = 2;
                Log("-AudioTranscoder: switched to bridged mode for codec %s.\n",
                    AudioCodec::GetNameFor( (AudioCodec::Type) packet.GetCodec()) );
            }
            else
            {
                state = 1;
                Log("-AudioTranscoder: switched to transcoder mode for codec %s.\n",
                    AudioCodec::GetNameFor( (AudioCodec::Type) packet.GetCodec()) );

            }
			
			recCodec = packet.GetCodec();
        }
        
        switch(state)
        {
            case 2: // Bridging
                encoder.Multiplex(packet);
                break;

            case 1:
            default:
                decoder.onRTPPacket(packet);
                break;

        }
    }
    else
    {
	decoder.onRTPPacket(packet);
    }
}
void AudioTranscoder::onResetStream()
{
	decoder.onResetStream();
}
void AudioTranscoder::onEndStream()
{
	decoder.onEndStream();
}


int AudioTranscoder::Attach(Joinable *join)
{
	
	if (!allowBridging)
    {   
		decoder.Attach(join);
    }
	else
    {
		decoder.Start();
		join->AddListener(this);
	}
}

int AudioTranscoder::Dettach()
{
	decoder.Dettach();
}

int AudioTranscoder::SetCodec(int codec)
{
    return encoder.SetCodec( (AudioCodec::Type) codec);
}

