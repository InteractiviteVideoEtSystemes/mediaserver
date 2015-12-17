/* 
 * File:   vad.h
 * Author: Sergio
 *
 * Created on 13 de agosto de 2012, 10:10
 */

#ifndef VAD_H
#define	VAD_H
#include "config.h"

class VADProxy
{
public:
	virtual DWORD GetVAD(int id) = 0;
};

#ifdef VADWEBRTC

extern "C" 
{
#include <common_audio/vad/vad_core.h>
}

/* packaged version with fedora core */
/* #include <webrtc_audio_processing/audio_processing.h> */


class VAD
{
public:
	typedef enum { QUALITY=0,LOWBITRATE=1,AGGRESSIVE=2,VERYAGGRESIVE=3} Mode;
public:
	VAD();
	
	bool SetMode(Mode mode);
	int CalcVad(SWORD* frame,DWORD size, DWORD rate);
	int GetVAD();
	bool IsRateSupported(DWORD rate ) { return ( rate == 8000 || rate == 16000 || rate == 32000 ); }
private:
	VadInstT inst; 
};
#else
class VAD
{
public:
	VAD(){};
	int CalcVad(SWORD* frame,DWORD size, DWORD rate) { return 0; }
	int GetVAD()			 { return 0; }
	bool IsRateSupported(DWORD rate ) { return 0; }
};
#endif
#endif	/* VAD_H */

