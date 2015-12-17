#ifndef G7221_H
#define	G7221_H
extern "C" {
#include <g722_1.h>
}
#include "config.h"
#include "fifo.h"
#include "codecs.h"
#include "audio.h"

class G7221Encoder : public AudioEncoder
{
public:
	G7221Encoder(const Properties &properties);
	virtual ~G7221Encoder();
	virtual int Encode(SWORD *in,int inLen,BYTE* out,int outLen);
	virtual DWORD TrySetRate(DWORD rate);
	virtual DWORD GetRate()			{ return rate;	}
	virtual DWORD GetClockRate()		{ return rate;	}
private:
	void OpenCodec();
	g722_1_encode_state_t state;
	DWORD rate;
	bool open;
};

class G7221Decoder : public AudioDecoder
{
public:
	G7221Decoder();
	virtual ~G7221Decoder();
	virtual int Decode(BYTE *in,int inLen,SWORD* out,int outLen);
	virtual DWORD TrySetRate(DWORD rate);
	virtual DWORD GetRate()			{ return rate;	}
private:
	g722_1_decode_state_t state;
	fifo<SWORD,1024>  samples;
	DWORD rate, bitrate;
	bool open;
	void OpenCodec();
};

#endif	/* G7221_H */

