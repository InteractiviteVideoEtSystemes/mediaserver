extern "C" {
#include <speex/speex.h>
#include <speex/speex_resampler.h>
}

#include "audio.h"

class SpeexEncoder : public AudioEncoder
{
public:
	SpeexEncoder(const Properties &properties);
	virtual ~SpeexEncoder();
	virtual int Encode(SWORD *in,int inLen,BYTE* out,int outLen);
	virtual DWORD TrySetRate(DWORD rate);
	virtual DWORD GetRate();
	virtual DWORD GetClockRate()	{ return 16000;}
private:
	void *encoder;
	SpeexBits encbits;
};

class SpeexDecoder : public AudioDecoder
{
public:
	SpeexDecoder();
	virtual ~SpeexDecoder();
	virtual int Decode(BYTE *in,int inLen,SWORD* out,int outLen);
	virtual DWORD TrySetRate(DWORD rate);
	virtual DWORD GetRate()		{ return 16000;}
private:
	void *decoder;
	SpeexResamplerState *wbresampler;
	SpeexBits decbits;
	int dec_frame_size;

};
