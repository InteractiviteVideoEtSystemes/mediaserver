#include "log.h"
#include "pipeaudiooutput.h"


PipeAudioOutput::PipeAudioOutput(bool calcVAD)
{
	//Store vad flag
	this->calcVAD = calcVAD;
	//No vad score acumulated
	acu = 0;
	//No rates yet
	nativeRate = 0;
	playRate = 0;
	//Creamos el mutex
	pthread_mutex_init(&mutex,NULL);
}

PipeAudioOutput::~PipeAudioOutput()
{
	//Lo destruimos
	pthread_mutex_destroy(&mutex);
}

int PipeAudioOutput::PlayBuffer(SWORD *buffer,DWORD size,DWORD frameTime)
{
	SWORD resampled[4096];
	DWORD resampledSize = 4096;
	int v = -1;

	//Check if we need to calculate it
	if (calcVAD && vad.IsRateSupported(playRate))
		//Calculate vad
		v = vad.CalcVad(buffer,size,playRate)*size;

	//Check if we are transtrating
	if (transrater.IsOpen())
	{
		//Proccess
		if (!transrater.ProcessBuffer( buffer, size, resampled, &resampledSize))
			//Error
			return Error("-PipeAudioOutput could not transrate\n");

		//Check if we need to calculate it
		if (calcVAD && v<0 && vad.IsRateSupported(nativeRate))
			//Calculate vad
			v = vad.CalcVad(resampled,resampledSize,nativeRate)*resampledSize;

		//Update parameters
		buffer = resampled;
		size = resampledSize;
	}

	//Bloqueamos
	pthread_mutex_lock(&mutex);

	//Get left space
	int left = fifoBuffer.size()-fifoBuffer.length();

	//if not enought
	if (size>left)
	{
		Log("-PipeAudioOutput: too much data in audio buffer. buf len = %d, to add = %lu\n", fifoBuffer.length(), size);
		//Free space
		fifoBuffer.remove(size-left);
	}

	//Get initial bump
	if (!acu && v)
		//Two seconds minimum
		acu+=16000;
	//Acumule VAD
	acu += v;

	//Check max
	if (acu>48000)
		//Limit so it can timeout faster
		acu = 48000;

	//Metemos en la fifo
	fifoBuffer.push(buffer,size);

	//Desbloqueamos
	pthread_mutex_unlock(&mutex);

	return (size <= left) ? size : -2;
}

int PipeAudioOutput::StartPlaying(DWORD rate)
{
	Log("-PipeAudioOutput start playing [rate:%d Hz]\n",rate);

	//Lock
	pthread_mutex_lock(&mutex);

	//Store play rate
	playRate = rate;

	//If we already had an open transcoder
	if (transrater.IsOpen())
		//Close it
		transrater.Close();

	//if rates are different
	if (playRate!=nativeRate)
		//Open it
		transrater.Open(playRate,nativeRate);
	
	//Unlock
	pthread_mutex_unlock(&mutex);
	
	//Exit
	return true;
}

int PipeAudioOutput::StopPlaying()
{
	Log("-PipeAudioOutput stop playing\n");

	//Lock
	pthread_mutex_lock(&mutex);
	//Close transrater
	transrater.Close();
	//Unlock
	pthread_mutex_unlock(&mutex);
	
	//Exit
	return true;
}

int PipeAudioOutput::GetSamples(SWORD *buffer,DWORD num,bool min_len)
{
	//Bloqueamos
	pthread_mutex_lock(&mutex);

	//Obtenemos la longitud
	int len = fifoBuffer.length();

	//Miramos si hay suficientes
	if (len > num)
		len = num;
	else if (len < num && !min_len)
	{
		//Desbloqueamos
		pthread_mutex_unlock(&mutex);
		return 0;
	}

	//OBtenemos las muestras
	fifoBuffer.pop(buffer,len);

	//Desbloqueamos
	pthread_mutex_unlock(&mutex);

	//Salimos
	return len;
}
int PipeAudioOutput::Init(DWORD rate)
{
	Log("-PipeAudioOutput init [rate:%d Hz]\n",rate);

	//Protegemos
	pthread_mutex_lock(&mutex);

	//Store play rate
	nativeRate = rate;

	//Iniciamos
	inited = true;

	//Protegemos
	pthread_mutex_unlock(&mutex);

	return true;
} 

int PipeAudioOutput::End()
{
	//Protegemos
	pthread_mutex_lock(&mutex);

	//Terminamos
	inited = false;

	//Se�alizamos la condicion
	//pthread_cond_signal(&newPicCond);

	//Protegemos
	pthread_mutex_unlock(&mutex);

	return true;
}

DWORD PipeAudioOutput::GetVAD(DWORD numSamples)
{
	//Protegemos
	pthread_mutex_lock(&mutex);
	//Get vad value
	DWORD r = acu;
	//Check
	if (acu<numSamples)
		//No vad
		acu = 0;
	else
		//Remove cumulative value
		acu -= numSamples;
	//Protegemos
	pthread_mutex_unlock(&mutex);
	
	//Return
	return r;
}
