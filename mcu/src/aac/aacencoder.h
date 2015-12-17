/* 
 * File:   aacencoder.h
 * Author: Sergio
 *
 * Created on 20 de junio de 2013, 10:46
 */

#ifndef AACENCODER_H
#define	AACENCODER_H


#include "config.h"
#include "codecs.h"
#include "audio.h"

typedef struct AVCodec AVCodec;
typedef struct AVCodecContext AVCodecContext;
typedef struct AVAudioResampleContext AVAudioResampleContext;

class AACEncoder : public AudioEncoder
{
public:
	AACEncoder(const Properties &properties);
	virtual ~AACEncoder();
	virtual int Encode(SWORD *in,int inLen,BYTE* out,int outLen);
	virtual DWORD TrySetRate(DWORD rate)	{ return GetRate();				}
	virtual DWORD GetRate()			{ return ctx->sample_rate?ctx->sample_rate:0;	}
	virtual DWORD GetClockRate()		{ return GetRate();				}
private:
	AVCodec 	*codec;
	AVCodecContext	*ctx;
	AVAudioResampleContext *avr;
	BYTE *samples;
	int samplesSize;
	int samplesNum;
};

#endif	/* AACENCODER_H */

