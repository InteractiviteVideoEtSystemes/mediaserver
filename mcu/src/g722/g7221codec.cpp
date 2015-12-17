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
#include "g7221codec.h"
#include "fifo.h"
#include "log.h"


G7221Encoder::G7221Encoder(const Properties &properties)
{
	//NO ctx yet
	memset(&state, 0, sizeof(state));
	///Set type
	type = AudioCodec::G7221;
	rate = 16000;			//Delfault sample rate
	open = false;
	OpenCodec();
}


void G7221Encoder::OpenCodec()
{
    g722_1_encode_state_t *s = NULL;
    numFrameSamples = 320;

    switch(rate)
    {
	case 16000:
	    // Check this. It looks wrong !
	    s = g722_1_encode_init(&state, 32000, rate);
	    break;
	    
	case 32000:
	    s = g722_1_encode_init(&state, 48000, rate);
	    break;
	    
    }
    
    if (s == NULL) 
	Error("g7221: failed to open encoder.\n");
    else
    {
	numFrameSamples = state.frame_size;
        open = true;
    }
}

DWORD G7221Encoder::TrySetRate(DWORD rate)
{
    this->rate = (rate >= 32000) ? 32000 : 16000;
    if (open)
    {
        g722_1_encode_release(&state);
	open = false;
    }

    OpenCodec();
    return this->rate;
}


G7221Encoder::~G7221Encoder()
{
    if (open)
    {
        g722_1_encode_release(&state);
	open = false;
    }     
}

int G7221Encoder::Encode (SWORD *in,int inLen,BYTE* out,int outLen)
{
	//If not enought samples
	if (inLen <= numFrameSamples)
		//Exit
		return Error("G7221: sample size %d is not correct. Should be %d\n", inLen, numFrameSamples);

		//For each one
	//Encode
	return g722_1_encode( &state, out, in, inLen);
}

G7221Decoder::G7221Decoder()
{
	//Set type
	type = AudioCodec::G7221;
	rate = 16000;			//Default sample rate
	bitrate = 32000;		//Use default
	memset(&state, 0, sizeof(state));
	open = false;
	OpenCodec();
	
}


void G7221Decoder::OpenCodec()
{
    g722_1_decode_state_t *s = NULL;
    numFrameSamples = 320;

    switch(rate)
    {
	case 16000:
	    if ( bitrate == 0 ) bitrate = 32000;
	    s = g722_1_decode_init(&state, bitrate, rate);
	    break;
	    
	case 32000:
	    if ( bitrate == 0 ) bitrate = 48000;
	    s = g722_1_decode_init(&state, bitrate, rate);
	    break;
	    
    }
    
    if (s == NULL) 
	Error("g7221: failed to open encoder.\n");
    else
    {
	numFrameSamples = state.frame_size;
        open = true;
    }
}

DWORD G7221Decoder::TrySetRate(DWORD rate)
{
    this->rate = (rate >= 32000) ? 32000 : 16000;
    if (open)
    {
        g722_1_decode_release(&state);
	open = false;
    }

    OpenCodec();
    return this->rate;
}

G7221Decoder::~G7221Decoder()
{
    if (open)
    {
        g722_1_decode_release(&state);
	open = false;
    }     
}

int G7221Decoder::Decode(BYTE *in, int inLen, SWORD* out, int outLen)
{
	int got_frame;
	SWORD buffer16[512];
	DWORD len16 = 512;
	
	//If we have input
	// TODO: inLen is big cut buffer into pieces
	if (inLen>0)
	{
	     len16 = g722_1_decode(&state, buffer16, in, inLen);
	     if (inLen > 0)
		samples.push(buffer16, len16);
	}
	//Check size
	if (samples.length()<numFrameSamples)
		//Nothing yet
		return 0;

	//Pop 160
	samples.pop(out,numFrameSamples);
	//Return number of samples
	return numFrameSamples;
}

