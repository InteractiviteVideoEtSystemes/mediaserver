/**
 * File:   G722codec.cpp
 * Author: Emmanuel BUU
 * (c) IVèS - released under GPL Licence
 *
 * 
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <cstdlib>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include "g722codec.h"
#include "fifo.h"
#include "log.h"

static void g722_packetdestruct(struct AVPacket * pkt)
{
    pkt->size = 0;
    pkt->data = NULL;
}

G722Encoder::G722Encoder(const Properties &properties)
{
	//NO ctx yet
	ctx = NULL;
	///Set type
	type = AudioCodec::G722;

	//Register all
	avcodec_register_all();

	// Get encoder
	codec = avcodec_find_encoder(AV_CODEC_ID_ADPCM_G722);

	// Check codec
	if(!codec)
	{
		Error("G722: No encoder found\n");
		return;
	}
	//Alocamos el conto y el picture
	ctx = avcodec_alloc_context3(codec);
	if (ctx == NULL)
	{
            Error("G722: could not allocate context.\n");
            return;
	}
	//Set params
	ctx->channels		= 1;
	ctx->sample_rate	= 16000;
	ctx->sample_fmt		=  AV_SAMPLE_FMT_S16;

	//OPEN it
	if (avcodec_open2(ctx, codec, NULL) < 0)
	{
		av_free(ctx);
		ctx = NULL;
	        Error("G722: could not open codec\n");
	}
	else
	{
		//Get the number of samples
		if ( ctx->frame_size > 0 ) 
			numFrameSamples = ctx->frame_size;
		else
		{
			numFrameSamples = 20 * 16; // 20 ms @ 16 Khz = 320 samples
			ctx->frame_size = numFrameSamples;
		}
                inSamples.nb_samples = numFrameSamples;
		Log("G722: Encoder open with frame size %d.\n", numFrameSamples);
	}

        av_init_packet(&outData);
        outData.destruct = g722_packetdestruct;
        //inSamples = avcodec_alloc_frame();
}

G722Encoder::~G722Encoder()
{
    av_free_packet(&outData);

    //Check
    if (ctx)
    {
        //Close
        avcodec_close(ctx);
        av_free(ctx);
    }
}

int G722Encoder::Encode (SWORD *in,int inLen,BYTE* out,int outLen)
{
        int got_packet_ptr, ret;
	if (ctx == NULL)
		return Error("G722: no context.\n");

	//If not enought samples
	if (inLen <= 0)
		//Exit
		return Error("G722: sample size %d is not correct. Should be %d\n", inLen, numFrameSamples);

	ret = avcodec_fill_audio_frame(&inSamples, ctx->channels, ctx->sample_fmt,
                                       (BYTE *) in, inLen*sizeof(SWORD), 0);

	if ( ret < 0 )
	{
		char strerr[256];

		av_strerror(ret, strerr, sizeof(strerr) ); 
		Error("G.722: failed to prepare %d samples for encoding. ffmpeg err: %d\n", inLen, ret);
		Error("G.722: %s.\n", strerr );
		return 0;
	}
        
        //Encode
        outData.data = out;
        outData.size = outLen;
        outData.destruct = NULL;

    ret = avcodec_encode_audio2(ctx, &outData, &inSamples, &got_packet_ptr);
    if ( ret == 0 )
    {
        if (got_packet_ptr)
        {
            return outData.size;
        }
        else
        {
            return 0;
        }
    }
    else
    {
        Error("G.722: encoding error ffmpeg errcode=%d, out buffer size=%d.\n", ret, outLen);
        return 0;
    }
}

G722Decoder::G722Decoder()
{
	//NO ctx yet
	ctx = NULL;
	///Set type
	type = AudioCodec::G722;

	//Register all
	avcodec_register_all();

	// Get encoder
	codec = avcodec_find_decoder(AV_CODEC_ID_ADPCM_G722);

	// Check codec
	if(!codec)
		Error("G722: No decoder found\n");

	//allocate codec cintext
	ctx = avcodec_alloc_context3(codec);

	//The resampler to convert to 8Khz
	int err;
	//Set params
	ctx->request_sample_fmt		= AV_SAMPLE_FMT_S16;
	//Set number of channels
	ctx->channels = 1;

	//OPEN it
	if (avcodec_open2(ctx, codec, NULL) < 0)
	{
		av_free(ctx);
		ctx = NULL;
	         Error("ffmpeg: could not open codec G.722\n");
	}
	else
	{
               if ( ctx->frame_size > 0 )
                        numFrameSamples = ctx->frame_size;
                else
                        numFrameSamples = 20 * 16; // 20 ms @ 16 Khz = 320 samples
                Log("G722: Decoder open with frame size %d.\n", numFrameSamples);
	}
}

G722Decoder::~G722Decoder()
{
	//Check
	if (ctx)
	{
		//Close
		avcodec_close(ctx);
		av_free(ctx);
	}
}

int G722Decoder::Decode(BYTE *in, int inLen, SWORD* out, int outLen)
{
	AVFrame frame;
	int got_frame;
	DWORD lenbuf;
	
	//If we have input
	if (inLen>0)
	{
		//Create packet
		AVPacket packet;

		//Init it
		av_init_packet(&packet);

		//Set data
		packet.data = (BYTE*)in;
		packet.size = inLen;

		//Decode it
		if (avcodec_decode_audio4(ctx,&frame,&got_frame,&packet)<0)
			//nothing
			return Error("Error decoding G.722\n");

		//If we got a frame
		if (got_frame)
		{
			//Get data
			SWORD *buffer16 = (SWORD *)frame.extended_data[0];
			DWORD len16 = frame.nb_samples;

			samples.push(buffer16,len16);
		}
		else
		{
		}

	}
	//Check size
	
	lenbuf = samples.length();
	if (lenbuf < numFrameSamples)
		//Nothing yet
		return 0;

	//Pop 160
	if ( outLen > lenbuf ) outLen = lenbuf;
	samples.pop(out,outLen);
	//Return number of samples
	return outLen;
}

