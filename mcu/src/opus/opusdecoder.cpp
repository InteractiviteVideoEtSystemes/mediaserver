/* 
 * File:   opusdecoder.cpp
 * Author: Sergio
 * 
 * Created on 14 de marzo de 2013, 11:19
 */

#include "opusdecoder.h"
#include "log.h"

OpusDecoder::OpusDecoder()
{
	int error = 0;
	//Set type
	type = AudioCodec::OPUS;
	//Set rate
	rate = 16000;
	//Create decoder
	dec = opus_decoder_create(rate,1,&error);
	//Check error
	if (!dec || error)
		Error("Could not open OPUS decoder: error=%d\n", error);
}

DWORD OpusDecoder::TrySetRate(DWORD rate)
{
	int error = 0;
	//Try to create a new decored with that rate
	OpusDecoder *aux = opus_decoder_create(rate,1,&error);
	//If no error
	if (aux && !error)
	{
		//Destroy old one
		if (dec) opus_decoder_destroy(dec);
		//Get new decoded
		dec = aux;
		//Store new rate
		this->rate = rate;
	}
        else
        {
            Error("OPUS: Could not open decoder with rate %d: error=%d",
                   rate, error);
        }
	
	//Return codec rate
	return this->rate;
}

OpusDecoder::~OpusDecoder()
{
	if (dec)
		opus_decoder_destroy(dec);
}

int OpusDecoder::Decode(BYTE *in,int inLen,SWORD* out,int outLen)
{
	//Decode without FEC
	if (inLen == 0)
		return 0;
	
	int ret = opus_decode(dec,in,inLen,out,outLen,0);
	//Check error
	if (ret<0)
		return Error("-Opus decode error [%d] inlen=%d \n",ret,inLen);
	//return decoded samples
	return ret;
}
