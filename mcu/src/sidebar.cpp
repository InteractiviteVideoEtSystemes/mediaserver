/* 
 * File:   sidebar.cpp
 * Author: Sergio
 * 
 * Created on 9 de agosto de 2012, 15:26
 */
#include <malloc.h>
#ifdef __SSE2__
#include <emmintrin.h>
#endif
#include <string.h>
#include "sidebar.h"
#include "log.h"

inline void* malloc32(size_t size)
{
	return memalign(32,size);
}

Sidebar::Sidebar()
{
	//Alloc alligned
	mixer_buffer = (SWORD*) malloc32(MIXER_BUFFER_SIZE*sizeof(SWORD));
}

Sidebar::~Sidebar()
{
	free(mixer_buffer);
}

int Sidebar::Update(int id,SWORD *samples,DWORD len)
{
	//Check size
	if (len>MIXER_BUFFER_SIZE)
		//error
		return Error("-Sidebar error updating participant, len bigger than mixer max buffer size [len:%d,size:%d]\n",len,MIXER_BUFFER_SIZE);
	//Check if
	if (participants.find(id)==participants.end())
		//Exit
		return Error("-Sidebar error updating participant not found [id:%d]\n",id);
#ifndef __SSE2__  // Sans SSE
	//Mix the audio
	for(int i = 0; i < len; ++i)
		//MIX
		mixer_buffer[i] += samples[i];
#else
	//Get pointers to buffer
	__m128i* d = (__m128i*) mixer_buffer;
	__m128i* s = (__m128i*) samples;

	//Sum 8 each time
	for(DWORD n = (len + 7) >> 3; n != 0; --n,++d,++s)
	{
		//Load data in SSE registers
		__m128i xmm1 = _mm_load_si128(d);
		__m128i xmm2 = _mm_load_si128(s);
		//SSE2 sum
		_mm_store_si128(d, _mm_add_epi16(xmm1,xmm2));
	}
#endif
	//OK
	return len;
}

void Sidebar::Reset()
{
	//zero the mixer buffer
	memset((BYTE*)mixer_buffer, 0, MIXER_BUFFER_SIZE*sizeof(SWORD));
}

void Sidebar::AddParticipant(int id)
{
	participants.insert(id);
}

void Sidebar::RemoveParticipant(int id)
{
	participants.erase(id);
}

bool Sidebar::HasParticipant(int id)
{
	//Check if
	if (participants.find(id)==participants.end())
		//Exit
		return false;
	//Found
	return true;
}


void Sidebar::UpdatePartVad(DWORD vadLevel)
{
    avgVad += vadLevel;
}
