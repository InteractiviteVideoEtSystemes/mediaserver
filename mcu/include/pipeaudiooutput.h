#ifndef _AUDIOOUTPUT_H_
#define _AUDIOOUTPUT_H_
#include <pthread.h>
#include <fifo.h>
#include <audio.h>
#include "vad.h"
#include "audiotransrater.h"

class PipeAudioOutput :
	public AudioOutput
	
{
public:
	PipeAudioOutput(bool calcVAD);
	~PipeAudioOutput();
	virtual int PlayBuffer(SWORD *buffer,DWORD size,DWORD frameTime);
	virtual int StartPlaying(DWORD samplerate);
	virtual int StopPlaying();

	virtual DWORD GetNativeRate()		{ return nativeRate;	}
	virtual DWORD GetPlayingRate()		{ return playRate;	}

	int GetSamples(SWORD *buffer,DWORD size,bool min_len=false);
	DWORD GetVAD(DWORD numSamples);
	int Init(DWORD samplerate);
	int End();
private:
	//Mutex
	pthread_mutex_t mutex;

	//Members
	fifo<SWORD,8192>	fifoBuffer;
	int			inited;
	VAD			vad;
	DWORD			acu;
	bool			calcVAD;
	AudioTransrater 	transrater;

	DWORD	playRate;
	DWORD	nativeRate;
};

#endif
