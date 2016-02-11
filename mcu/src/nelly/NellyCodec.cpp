/* 
 * File:   NellyCodec.cpp
 * Author: Sergio
 * 
 * Created on 7 de diciembre de 2011, 23:29
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <cstdlib>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include "NellyCodec.h"
#include "fifo.h"
#include "log.h"

NellyEncoder::NellyEncoder(const Properties &properties)
{
	//NO ctx yet
	ctx = NULL;
	///Set type
	type = AudioCodec::NELLY8;

	//Register all
	avcodec_register_all();

	// Get encoder
	codec = avcodec_find_encoder(AV_CODEC_ID_NELLYMOSER);

	// Check codec
	if(!codec)
		Error("No codec found\n");

	//Alocamos el conto y el picture
	ctx = avcodec_alloc_context3(codec);

	//Set params
	ctx->channels		= 1;
	ctx->sample_rate	= 8000;
	ctx->sample_fmt		= AV_SAMPLE_FMT_FLT;

	//OPEN it
	if (avcodec_open2(ctx, codec, NULL) < 0)
	         Error("could not open codec\n");

	//Get the number of samples
	numFrameSamples = ctx->frame_size;
}

NellyEncoder::~NellyEncoder()
{
	//Check
	if (ctx)
		//Close
		avcodec_close(ctx);
}

int NellyEncoder::Encode (SWORD *in,int inLen,BYTE* out,int outLen)
{
	SWORD buffer[512];
	DWORD len = 512;
	float bufferf[512];

	//Put samples back
	samples.push(in,inLen);
	//If not enought samples
	if (samples.length()<ctx->frame_size)
		//Exit
		return 0;
	//Get them
	samples.pop(buffer,ctx->frame_size);
	//For each one
	for (int i=0;i<ctx->frame_size;++i)
		//Convert to float
		bufferf[i] = ((float) buffer[i] / 32767.0f );

	//Encode
	return avcodec_encode_audio(ctx, out, outLen, (SWORD*)bufferf);
}


NellyEncoder11Khz::NellyEncoder11Khz(const Properties &properties)
{
	//NO ctx yet
	ctx = NULL;
	///Set type
	type = AudioCodec::NELLY11;

	//Register all
	avcodec_register_all();

	// Get encoder
	codec = avcodec_find_encoder(AV_CODEC_ID_NELLYMOSER);

	// Check codec
	if(!codec)
		Error("No codec found\n");

	//Alocamos el conto y el picture
	ctx = avcodec_alloc_context3(codec);

	
	//Set params
	ctx->channels		= 1;
	ctx->sample_rate	= 11025;//11025;11025
	ctx->sample_fmt		= AV_SAMPLE_FMT_FLT;

	//OPEN it
	if (avcodec_open2(ctx, codec, NULL) < 0)
	         Error("could not open codec\n");

	//Get the number of samples
	numFrameSamples = ctx->frame_size;
}

NellyEncoder11Khz::~NellyEncoder11Khz()
{
	//Check
	if (ctx)
		//Close
		avcodec_close(ctx);
}

DWORD NellyEncoder11Khz::TrySetRate(DWORD rate)
{
	//return real rate
	return GetRate();
}


int NellyEncoder11Khz::Encode (SWORD *in,int inLen,BYTE* out,int outLen)
{
	SWORD buffer11[512];
	DWORD len11 = 512;
	float bufferf[512];

	//If we have data
	if (inLen>0)
	{
		//Push
		samples11.push(in,inLen);
	}

	//If not enought samples
	if (samples11.length()<ctx->frame_size)
		//Exit
		return 0;
	//Get them
	samples11.pop(buffer11,ctx->frame_size);
	//For each one
	for (int i=0;i<ctx->frame_size;++i)
	{
		//Convert to float and apply a gain
		bufferf[i] = (((float) buffer11[i])*5.0f) / (32767.0f);
		//if ( bufferf[i] > 1.0f) bufferf[i] = 1.0f;
		//else if ( bufferf[i] < -1.0f) bufferf[i] = -1.0f;
	}
	//Encode
	return avcodec_encode_audio(ctx, out, outLen, (SWORD*)bufferf);
}

NellyDecoder11Khz::NellyDecoder11Khz()
{
	//NO ctx yet
	ctx = NULL;
	///Set type
	type = AudioCodec::NELLY11;

	//Register all
	avcodec_register_all();

	// Get encoder
	codec = avcodec_find_decoder(AV_CODEC_ID_NELLYMOSER);

	// Check codec
	if(!codec)
		Error("No codec found\n");

	//Alocamos el conto y el picture
	ctx = avcodec_alloc_context3(codec);

	//Set params
	ctx->request_sample_fmt		= AV_SAMPLE_FMT_S16;
	//OPEN it
	if (avcodec_open2(ctx, codec, NULL) < 0)
	         Error("could not open codec\n");

	//Set number of channels
	ctx->channels = 1;
	
	//Get the number of samples
	if ( ctx->frame_size > 0 ) 
		numFrameSamples = ctx->frame_size;
	else
	{
		numFrameSamples = 256;
		ctx->frame_size = numFrameSamples;
	}
}

NellyDecoder11Khz::~NellyDecoder11Khz()
{
	//Check
	if (ctx)
		//Close
		avcodec_close(ctx);
}

DWORD NellyDecoder11Khz::TrySetRate(DWORD rate)
{


	return GetRate();
}

int NellyDecoder11Khz::Decode(BYTE *in, int inLen, SWORD* out, int outLen)
{
	int got_frame;
	SWORD buffer11[512]; 
	//If we have input
	if (inLen>0)
	{
		//Create packet
		AVPacket packet;
		AVFrame* frame = av_frame_alloc();
		//Init it
		av_init_packet(&packet);

		//Set data
		packet.data = (BYTE*)in;
		packet.size = inLen;

		//Decode it
		if (avcodec_decode_audio4(ctx,frame,&got_frame,&packet)<0)
		{
			av_frame_free(&frame);
			//nothing
			return Error("Error decoding nellymoser\n");
        }
		
		//If we got a frame
		if (got_frame)
		{
			//Get data
			float *fbuffer11 = (float *) frame.extended_data[0];

			//Convert to SWORD
			DWORD len11 = 0;
			for (int i=0; i<frame.nb_samples; ++i)
			{   
				buffer11[len11++] = (SWORD) (fbuffer11[i] * 32767.0f * 0.8f);
				if (len11 > 512)
				{
					// Internal buffer is full, flush them
					samples.push(buffer11,len11);
					len11 = 0;
				}
			}
			
			//Append to samples
			samples.push(buffer11,len11);
		}
		av_frame_free(&frame);
	}

        DWORD lenbuf = samples.length();
        if (lenbuf < numFrameSamples)
        //Nothing yet
            	return 0;

        //return decoded samples
        if ( outLen > lenbuf ) outLen = lenbuf;
        	samples.pop(out,outLen);

	//Return number of samples
	return outLen;
}

