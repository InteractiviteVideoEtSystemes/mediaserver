#include <signal.h>
#include <sys/time.h>
#include <stdio.h>
#ifdef __SSE2__
#include <emmintrin.h>
#endif
#include <set>
#include "log.h"
#include "tools.h"
#include "audiomixer.h"
#include "pipeaudioinput.h"
#include "pipeaudiooutput.h"
#include "sidebar.h"

/***********************
* AudioMixer
*	Constructor
************************/
AudioMixer::AudioMixer()
{
	//Not mixing
	mixingAudio = 0;
	//No sidebars
	numSidebars = 0;
	//NO vad by default
	vad = false;
}

/***********************
* ~AudioMixer
*	Destructor
************************/
AudioMixer::~AudioMixer()
{
}

/***********************************
* startMixingAudio
*	Crea el thread de mezclado de audio
************************************/
void *AudioMixer::startMixingAudio(void *par)
{
	Log("-MixAudioThread [%d]\n",getpid());

	//Obtenemos el parametro
	AudioMixer *am = (AudioMixer *)par;

	//Bloqueamos las se�ales
	blocksignals();
	
	//Ejecutamos
	pthread_exit((void *)am->MixAudio());
}

/***********************************
* MixAudio
*	Mezcla los audios
************************************/
int AudioMixer::MixAudio()
{
	timeval  tv;
	DWORD step = 10;
	QWORD prev = 0;
	DWORD avgVad;
	int overtimeCount;
	//Logeamos
	Log(">MixAudio\n");

	//Init ts
	getUpdDifTime(&tv);
	overtimeCount = 0;
	//Mientras estemos mezclando
	while(mixingAudio)
	{
		//Get processing time
		DWORD proc = getDifTime(&tv)-prev;
		
		//check we have not to hurry up
		if (proc<step*1000)
		{
			//Wait until next to process again minus process time
			msleep(step*1000-proc);
			if (overtimeCount > 0)
			{
				overtimeCount--;
			}
		}
		else
		{
			if ( overtimeCount == 0) Log("-Audiomixer taking too much time, hurrying up [%u]\n",proc);
			overtimeCount++;
		}
			

		//Block list
		lstAudiosUse.WaitUnusedAndLock();

		//Get new time
		QWORD curr = getDifTime(&tv);

		//Get num samples at desired rate for the time difference
		DWORD numSamples = (curr*rate)/1000000-(prev*rate)/1000000;

		//Update curr
		prev = curr;

		//At most the maximum
		if (numSamples>Sidebar::MIXER_BUFFER_SIZE)
		{
			//Log
			Log("-AudiMixer num mixing samples bigger than buffer [%d]\n",numSamples);
			//Set it at most (shoudl never happen)
			numSamples = Sidebar::MIXER_BUFFER_SIZE;
		}

		//For each sidepar
		for (Sidebars::iterator sit=sidebars.begin(); sit!=sidebars.end(); ++sit)
			//REset
			sit->second->Reset();
		
		//First pass: Iterate through the audio inputs and calculate the sum of all streams
		for(Audios::iterator it = audios.begin(); it != audios.end(); ++it)
		{
			//Get the source
			AudioSource *audio = it->second;
			//Get id
			DWORD id = it->first;
			//Get the samples from the fifo
			audio->len = audio->output->GetSamples(audio->buffer,numSamples);
			//Clean rest
			memset(audio->buffer+audio->len,0,Sidebar::MIXER_BUFFER_SIZE-audio->len);
			//Get VAD value
			audio->vad = audio->output->GetVAD(numSamples);
			
#if 0
			if (vad)
			{
			    for (Sidebars::iterator sit = sidebars.begin(); sit!=sidebars.end(); ++sit)
				//Mix it and update length
				if ( sit->second->HasParticipant(id) ) sit->second->UpdatePartVad(audio->vad);
			}
#endif
		}

		for(Audios::iterator it = audios.begin(); it != audios.end(); ++it)
		{
			//Get the source
			AudioSource *audio = it->second;
			//Get id
			DWORD id = it->first;

			for (Sidebars::iterator sit = sidebars.begin(); sit!=sidebars.end(); ++sit)
				//Mix it and update length
			    if ( audio->len > 0 && sit->second->HasParticipant(id) )
				audio->len = sit->second->Update(id,audio->buffer,audio->len);
		}
		
		// Second pass: Calculate this stream's output
		for(Audios::iterator it = audios.begin(); it != audios.end(); it++)
		{
			//Get the source
			AudioSource *audio = it->second;
			//Get id
			DWORD id = it->first;
			//Check audio
			if (!audio)
				//Next
				continue;
			//Check sidebar
			if (!audio->sidebar)
				//Next
				continue;
			//Get mixed buffer
			SWORD *mixed = audio->sidebar->GetBuffer();
			//And the audio buffer for participant
			SWORD *buffer = audio->buffer;

			//Check if we have been added to the sidebar
			if (audio->sidebar->HasParticipant(id))
			{
#ifndef __SSE2__
				// Mixing witjout SSE
				//Calculate the result
				for(int i=0; i<audio->len; ++i)
					//We don't want to hear our own signal
					buffer[i] = mixed[i] - buffer[i];
#else
				//Get pointers to buffer
				__m128i* b = (__m128i*) buffer;
				__m128i* m = (__m128i*) mixed;

				//Sum 8 ech time
				for(DWORD n = (audio->len + 7) >> 3; n != 0; --n,++b,++m)
				{
					//Load data in SSE registers
					__m128i xmm1 = _mm_load_si128(m);
					__m128i xmm2 = _mm_load_si128(b);
					//SSE2 sum
					_mm_store_si128(b,  _mm_sub_epi16(xmm1,xmm2));
				}
#endif
				//Check length
				if (audio->len<numSamples)
					//Copy the rest
					memcpy(buffer+audio->len,mixed+audio->len,(numSamples-audio->len)*sizeof(SWORD));
				//Put the output
				audio->input->PutSamples(buffer,numSamples);
			} else {
				//Copy everything as it is
				audio->input->PutSamples(mixed,numSamples);
			}
		}

		//Unblock list
		lstAudiosUse.Unlock();
	}

	//Logeamos
	Log("<MixAudio\n");

	return 1;
}

/***********************
* Init
*	Inicializa el mezclado de audio
************************/
int AudioMixer::Init(bool vad,DWORD rate)
{
	Log("-Init audio mixer [vad:%d,rate:%d]\n",vad,rate);

	//Store if we need to use vad or not
	this->vad = vad;

	//Check rate
	if (rate == 8000 || rate == 16000 || rate == 32000 || rate == 48000)
	{
		//Store rate
		this->rate = rate;
	}
	else
	{
		return Error("Unsupported rate. Musr be 8000, 16000 or 32000 Hz.\n");
	}

	// Estamos mzclando
	mixingAudio = true;

	//Create default sidebar
	int id = CreateSidebar();

	//Set default
	defaultSidebar = sidebars[id];

	//Y arrancamoe el thread
	createPriorityThread(&mixAudioThread,startMixingAudio,this,0);

	return 1;
}


/***********************
* End
*	Termina el mezclado de audio
************************/
int AudioMixer::End()
{
	Log(">End audiomixer\n");


	//Terminamos con la mezcla
	if (mixingAudio)
	{
		//Terminamos la mezcla
		mixingAudio = 0;

		//Y esperamos
		pthread_join(mixAudioThread,NULL);
	}

	//Lock
	lstAudiosUse.WaitUnusedAndLock();

	//Recorremos la lista
	for (Audios::iterator it =audios.begin();it!=audios.end();++it)
	{
		//Obtenemos el audio source
		AudioSource *audio = it->second;

		//Terminamos
		audio->input->End();
		audio->output->End();
		
		//SI esta borramos los objetos
		delete audio->input;
		delete audio->output;
		delete audio;
	}

	//Clear list
	audios.clear();

	//For each sidebar
	for (Sidebars::iterator it=sidebars.begin(); it!=sidebars.end();++it)
	{
		//Get sidebar
		Sidebar *sidebar = it->second;
		//Delete the sidebar
		delete(sidebar);
	}

	//Clear list
	sidebars.clear();

	//Unlock
	lstAudiosUse.Unlock();
	
	Log("<End audiomixer\n");
	
	return 1;
}

/***********************
* CreateMixer
*	Crea una nuevo source de audio para mezclar
************************/
int AudioMixer::CreateMixer(int id)
{
	Log(">CreateMixer audio [%d]\n",id);

	//Protegemos la lista
	lstAudiosUse.WaitUnusedAndLock();

	//Miramos que si esta
	if (audios.find(id)!=audios.end())
	{
		//Desprotegemos la lista
		lstAudiosUse.Unlock();
		return Error("Audio sourecer already existed\n");
	}

	//Creamos el source
	AudioSource *audio = new AudioSource();

	//POnemos el input y el output
	audio->input  = new PipeAudioInput();
	audio->output = new PipeAudioOutput(vad);
	//No sidebar yet
	audio->sidebar = NULL;
	//Clean buffer
	memset(audio->buffer, 0, Sidebar::MIXER_BUFFER_SIZE*sizeof(SWORD));
	audio->len = 0;
	audio->vad = 0;

	//Y lo a�adimos a la lista
	audios[id] = audio;

	//Desprotegemos la lista
	lstAudiosUse.Unlock();

	//Y salimos
	Log("<CreateMixer audio\n");

	return true;
}

/***********************
* InitMixer
*	Inicializa un audio
*************************/
int AudioMixer::InitMixer(int id,int sidebarId)
{

	Log(">Audio mixer: partId=%d will listen to sidebar %d\n",id, sidebarId);

	//Protegemos la lista
	lstAudiosUse.IncUse();

	//Buscamos el audio source
	Audios::iterator it = audios.find(id);

	//Si no esta	
	if (it == audios.end())
	{
		//Desprotegemos
		lstAudiosUse.DecUse();
		//Salimos
		return Error("Mixer not found\n");
	}

	//Obtenemos el audio source
	AudioSource *audio = it->second;

	//Get the sidebar for the user
	Sidebars::iterator itSidebar = sidebars.find(sidebarId);

	//If found
	if (itSidebar!=sidebars.end())
		//Set it
		audio->sidebar = itSidebar->second;
	else
		//Send only participant
		Log("-No sidebar %d for participant found, will be send only.\n", sidebarId);

	//INiciamos los pipes
	audio->input->Init(rate);
	audio->output->Init(rate);

	//Add participant to default sidebar
	defaultSidebar->AddParticipant(id);

	//Desprotegemos
	lstAudiosUse.DecUse();

	Log("<Init mixer [%d]\n",id);

	//Si esta devolvemos el input
	return true;
}


/***********************
* EndMixer
*	Finaliza un audio
*************************/
int AudioMixer::EndMixer(int id)
{
	//Protegemos la lista
	lstAudiosUse.WaitUnusedAndLock();

	//Buscamos el audio source
	Audios::iterator it = audios.find(id);

	//Si no esta	
	if (it == audios.end())
	{
		//Desprotegemos
		lstAudiosUse.Unlock();
		//Salimos
		return false;
	}

	//Remvoe participant to default sidebar
	defaultSidebar->RemoveParticipant(id);

	//Obtenemos el audio source
	AudioSource *audio = it->second;

	//Terminamos
	audio->input->End();
	audio->output->End();

	//Unset sidebar
	audio->sidebar = NULL;

	//For all the sidebars
	for (Sidebars::iterator it = sidebars.begin(); it!=sidebars.end(); ++it)
		//Remove particiapant
		it->second->RemoveParticipant(id);

	//Desprotegemos
	lstAudiosUse.Unlock();

	//Si esta devolvemos el input
	return true;;
}

/***********************
* DeleteMixer
*	Borra una fuente de audio
************************/
int AudioMixer::DeleteMixer(int id)
{
	Log("-DeleteMixer audio [%d]\n",id);

	//Protegemos la lista
	lstAudiosUse.WaitUnusedAndLock();

	//Lo buscamos
	Audios::iterator it = audios.find(id);

	//SI no ta
	if (it == audios.end())
	{
		//DDesprotegemos la lista
		lstAudiosUse.Unlock();
		//Salimos
		return Error("Audio source not found\n");
	}

	//Obtenemos el audio source
	AudioSource *audio = it->second;

	//Lo quitamos de la lista
	audios.erase(it);

	//Desprotegemos la lista
	lstAudiosUse.Unlock();

	//SI esta borramos los objetos
	delete audio->input;
	delete audio->output;
	delete audio; 

	return 0;
}

/***********************
* GetInput
*	Obtiene el input para un id
************************/
AudioInput* AudioMixer::GetInput(int id)
{
	//Protegemos la lista
	lstAudiosUse.IncUse();

	//Buscamos el audio source
	Audios::iterator it = audios.find(id);

	//Obtenemos el input
	AudioInput *input = NULL;

	//Si esta
	if (it != audios.end())
		input = it->second->input;

	//Desprotegemos
	lstAudiosUse.DecUse();

	//Si esta devolvemos el input
	return input;
}

/***********************
* GetOutput
*	Obtiene el output para un id
************************/
AudioOutput* AudioMixer::GetOutput(int id)
{
	//Protegemos la lista
	lstAudiosUse.IncUse();

	//Buscamos el audio source
	Audios::iterator it = audios.find(id);

	//Obtenemos el output
	AudioOutput *output = NULL;

	//Si esta	
	if (it != audios.end())
		output = it->second->output;

	//Desprotegemos
	lstAudiosUse.DecUse();

	//Si esta devolvemos el input
	return output;
}

/***********************************
 * SetMixerSidebar
 *	Add a participant to be shown in a sidebar
 ************************************/
int AudioMixer::SetMixerSidebar(int id,int sidebarId)
{
	Log(">SetMixerSidebar [id:%d,sidebar:%d]\n",id,sidebarId);

	//Protegemos la lista
	lstAudiosUse.IncUse();

	//Buscamos el audio source
	Audios::iterator it = audios.find(id);

	//Si no esta
	if (it == audios.end())
	{
		//Desprotegemos
		lstAudiosUse.DecUse();
		//Salimos
		return Error("Mixer not found\n");
	}

	//Obtenemos el audio source
	AudioSource *audio = it->second;

	//Get the sidebar for the user
	Sidebars::iterator itSidebar = sidebars.find(sidebarId);

	//If found
	if (itSidebar!=sidebars.end())
		//Set sidebar
		audio->sidebar = itSidebar->second;
	else
		//Send only participant
		Log("-Sidebar %d for participant found, will be send only.\n",
                    sidebarId );

	//Desprotegemos
	lstAudiosUse.DecUse();

	Log("<SetMixerSidebar [%d]\n",id);

	//Si esta devolvemos el input
	return true;
}


int AudioMixer::GetMixerSidebar(int id)
{
	Log(">GetMixerSidebar [id:%d,sidebar:%d]\n",id);

	//Protegemos la lista
	lstAudiosUse.IncUse();

	//Buscamos el audio source
	Audios::iterator it = audios.find(id);

	//Si no esta
	if (it == audios.end())
	{
		//Desprotegemos
		lstAudiosUse.DecUse();
		//Salimos
		Error("Mixer not found\n");
                return -1;
	}

	//Obtenemos el audio source
	AudioSource *audio = it->second;

        for ( Sidebars::iterator it = sidebars.begin();
              it != sidebars.end();
              it++ )
        {
            if (audio->sidebar ==  it->second)
            {
                lstAudiosUse.DecUse();
                return it->first;
            }
        }

        lstAudiosUse.DecUse();

        return -1;
}


/***********************************
 * AddSidebarParticipant
 *	Add a participant to be shown in a sidebar
 ************************************/
int AudioMixer::AddSidebarParticipant(int sidebarId, int partId)
{
	Log("-AddSidebarParticipant [sidebar:%d,partId:%d]\n",sidebarId,partId);

	//Block
	lstAudiosUse.IncUse();

	//Get the sidebar for the user
	Sidebars::iterator itSidebar = sidebars.find(sidebarId);

	//If not found
	if (itSidebar==sidebars.end())
	{
		//UnBlock
		lstAudiosUse.DecUse();
		//Salimos
		return Error("Sidebar not found\n");
	}

	//Add participant to the sidebar
	itSidebar->second->AddParticipant(partId);

	//UnBlock
	lstAudiosUse.DecUse();

	//Everything ok
	return 1;
}

/***********************************
 * RemoveSidebarParticipant
 *	Remove a participant to be shown in a sidebar
 ************************************/
int AudioMixer::RemoveSidebarParticipant(int sidebarId, int partId)
{
	Log(">-RemoveSidebarParticipant [sidebar:%d,partId:%d]\n",sidebarId,partId);

	//Block
	lstAudiosUse.IncUse();
	
	//Get the sidebar for the user
	Sidebars::iterator itSidebar = sidebars.find(sidebarId);

	//If not found
	if (itSidebar==sidebars.end())
	{
		//UnBlock
		lstAudiosUse.DecUse();
		//Salimos
		return Error("Sidebar not found\n");
	}

	//Get sidebar
	Sidebar* sidebar = itSidebar->second;

	//Remove participant to the sidebar
	sidebar->RemoveParticipant(partId);

	//UnBlock
	lstAudiosUse.DecUse();
		
	//Correct
	return 1;
}

int AudioMixer::CreateSidebar()
{
	//Get id
	int id = numSidebars++;

	//Block
	lstAudiosUse.IncUse();

	//add it
	sidebars[id] = new Sidebar();

	//UnBlock
	lstAudiosUse.DecUse();

	return id;
}

int AudioMixer::DeleteSidebar(int sidebarId)
{

	//Block
	lstAudiosUse.WaitUnusedAndLock();

	//Get sidebar from id
	Sidebars::iterator it = sidebars.find(sidebarId);

	//Check if we have found it
	if (it==sidebars.end())
	{
		//UnBlock
		lstAudiosUse.Unlock();
		//error
		return Error("Sidebar not found [id:%d]\n",sidebarId);
	}

	//Get the old sidebar
	Sidebar *sidebar = it->second;

	//For each audio
	for (Audios::iterator ita = audios.begin(); ita!= audios.end(); ++ita)
	{
		//Check it it has dis sidebar
		if (ita->second->sidebar == sidebar)
			//Set to null
			ita->second->sidebar = NULL;
	}

	//Remove sidebar
	sidebars.erase(it);

	//UnBlock
	lstAudiosUse.Unlock();

	//Delete sidebar
	delete(sidebar);

	//Exit
	return 1;
}

DWORD AudioMixer::GetVAD(int id)
{
	DWORD acuVAD = 0;

	//Lock
	lstAudiosUse.IncUse();

	//Find it
	Audios::iterator it = audios.find(id);

	//If found
	if (it!=audios.end())
		//Get vad
		acuVAD = it->second->vad;

	//Unlock
	lstAudiosUse.DecUse();

	//Return VAD acumulated
	return acuVAD;
}

int AudioMixer::DumpMixerInfo(int sidebarId, std::string & info)
{
        lstAudiosUse.IncUse();
        char partId[40];
        
	Sidebars::iterator it = sidebars.find(sidebarId);

	//Check if we have found it
	if (it==sidebars.end())
	{
		//UnBlock
		lstAudiosUse.DecUse();
		//error
                sprintf(partId, "Sidebar %d: no such sidebar\n", sidebarId);
                info += partId;
		return 200;
	}

        std::set<int> parts = it->second->GetParticipants();
        
        
        sprintf(partId, "Sidebar %d (vadmode=%d): ", sidebarId, vad);
        info += partId;
        if (parts.empty()) info += "no participant";

        for (std::set<int>::iterator it2 = parts.begin(); 
             it2 != parts.end();
             it2++)
        {
            sprintf(partId, "%d ", *it2);
            info += partId;
        }

        info += "\n";

        lstAudiosUse.DecUse();
        return 200;
}
