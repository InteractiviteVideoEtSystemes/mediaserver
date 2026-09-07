#include <signal.h>
#include <sys/time.h>
#include <stdio.h>
#include <tools.h>
#include <wchar.h>
#include "log.h"
#include "textmixer.h"
#include "pipetextinput.h"
#include "pipetextoutput.h"


/***********************
* TextMixer
*	Constructor
************************/
TextMixer::TextMixer()
{
}

/***********************
* ~TextMixer
*	Destructor
************************/
TextMixer::~TextMixer()
{
	//Contrat Worker : arrêter le thread avant de détruire l'état dérivé
	StopThread();
}

/***********************************
* Run
*	Mezcla los texts (corps du Worker)
************************************/
int TextMixer::Run()
{
	wchar_t buffer[1024];
	DWORD size=1024;

	Log(">MixText\n");

	//Mientras estemos mezclando. `IsThreadRunning()` fait partie de la condition
	//parce que le contrat de worker.h l'exige : sans lui, le StopThread() du
	//destructeur joint un thread que seul End() peut arreter, et pend.
	while(mixingText && IsThreadRunning())
	{
		//Lock list of text mixers
		//La passe entiere tient le verrou : elle itere les trois collections ET
		//appelle les workers, qui ne se protegent pas eux-memes.
		std::unique_lock<std::mutex> mixLock(mutex);

		//Send to all participants
		for (TextSources::iterator it=sources.begin();it!=sources.end();++it)
		{
			//Get text source
			const std::shared_ptr<TextSource>& text = it->second;

			//Check if it has somethin in the queue
			if (text->output->Length())
			{
				//Read input
				DWORD len = text->output->ReadText(buffer,size);
				//Add to all workers
				for (TextWorkers::iterator w=workers.begin();w!=workers.end();++w)
				{
					//Get worker
					const std::shared_ptr<TextMixerWorker>& worker = (*w);
					//Check it is not the ixer worker
					if (text->worker!=worker)
						//Write it
						worker->WriteText(text->id,buffer,len);
				}
			}
		}

		//Send private texts
		for (TextPrivates::iterator it=privates.begin();it!=privates.end();++it)
		{
			//Get text source
			const std::shared_ptr<TextPrivate>& priv = it->second;

			//Check if it has somethin in the queue
			if (priv->output->Length())
			{
				//Read input
				DWORD len = priv->output->ReadText(buffer,size);
				//Get private worked
				//Add to all workers
				TextSources::iterator its = sources.find(priv->to);
				//If it setill exist
				if (its!=sources.end())
					//Write it
					its->second->worker->WriteText(priv->id,buffer,len);
			}
		}

		//Know for each worker
		for (TextWorkers::iterator w=workers.begin();w!=workers.end();++w)
			//Process it
			(*w)->ProcessText();

		//Un lock
		//L'attente se fait HORS verrou : sinon rien ne pourrait plus entrer ni
		//sortir de la conference pendant les 200 ms du tick.
		mixLock.unlock();

		//Tick de 200 ms, interruptible par End()/StopThread()
		wait.WaitSignal(200);
	}

	//Know for each worker
	{
		std::unique_lock<std::mutex> flushLock(mutex);
		for (TextWorkers::iterator w=workers.begin();w!=workers.end();++w)
			//Flush any text in the queue
			(*w)->FlushText();
	}

	//Logeamos
	Log("<MixText\n");

	return 1;
}


/***********************
* Init
*	Inicializa el mezclado de text
************************/
int TextMixer::Init()
{
	// Estamos mzclando
	mixingText = true;

	//Y arrancamoe el thread (réarme le Wait interne)
	StartThread();

	return 1;
}

/***********************
* End
*	Termina el mezclado de text
************************/
int TextMixer::End()
{
	Log(">End textmixer\n");

	//Terminamos con la mezcla
	if (mixingText)
	{
		//Terminamos la mezcla
		mixingText = 0;

		//Réveille le tick et joint (arrêt immédiat)
		StopThread();
	}

	//Borramos los texts restantes. DeleteMixer EFFACE l'entrée de la map :
	//itérer dessus pendant la suppression invalidait l'itérateur (UB, crash
	//constaté par TextMixerSite.ForwardsTextToOtherParticipantOnly). On relève
	//donc une clé sous le verrou, et on supprime en dehors — DeleteMixer le
	//reprend pour son propre compte.
	for (;;)
	{
		DWORD id;
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (sources.empty())
				break;
			id = sources.begin()->first;
		}
		//Borramos el text
		DeleteMixer(id);
	}

	Log("<End textmixer\n");
	
	return 1;
}

/***********************
* CreateMixer
*	Crea una nuevo source de text para mezclar
************************/
int TextMixer::CreateMixer(int id,std::wstring &name)
{
	Log(">CreateMixer text [%d]\n",id);

	//Protegemos la lista
	std::unique_lock<std::mutex> lock(mutex);

	//Miramos que si esta
	if (sources.find(id)!=sources.end())
	{
		//Desprotegemos la lista
		lock.unlock();
		return Error("Text sourecer already existed\n");
	}

	//Creamos el source
	std::shared_ptr<TextSource> text = std::make_shared<TextSource>();

	//Set id
	text->id = id;

	//POnemos el input y el output (co-propriété shared_ptr, Point 1 / C-4)
	text->input  = std::make_shared<PipeTextInput>();
	text->output = std::make_shared<PipeTextOutput>();
	//Create the worker
	text->worker = std::make_shared<TextMixerWorker>();

	//Set name
	text->name = name;

	//Add source to the list
	sources[id] = text;

	Log("-Text [%d,%ls]\n",text->id,text->name.c_str());

	//Desprotegemos la lista
	lock.unlock();

	//Y salimos
	Log("<CreateMixer text\n");

	return true;
}

/***********************
* SetDisplayName
*	Cambia la etiqueta mostrada para un participante
*************************/
int TextMixer::SetDisplayName(int id,const std::wstring &displayName)
{
	//Verrou EXCLUSIF, comme AddWritter : on modifie les workers que le thread
	//de mixage parcourt, et TextMixerWorker n'a aucune synchronisation propre.
	std::unique_lock<std::mutex> lock(mutex);

	//Buscamos el text source
	TextSources::iterator it = sources.find(id);

	//Si no esta
	if (it == sources.end())
		return Error("Text source not found [%d]\n",id);

	//L'etiquette est dupliquee dans le worker de chaque AUTRE participant : la
	//source la garde pour ceux qui rejoindront ensuite.
	it->second->displayName = displayName;

	for (TextWorkers::iterator itWorker=workers.begin();itWorker!=workers.end();++itWorker)
		//Le worker du participant lui-meme ne le porte pas : il rend 0, sans faute
		(*itWorker)->SetWritterDisplayName(id,displayName);

	//Le display name ne s'imprime pas : Log passe par %ls, et la locale C
	//tronque la ligne au premier caractere non ASCII.
	Log("-SetDisplayName text [%d]\n",id);

	return 1;
}

/***********************
* InitMixer
*	Inicializa un text
*************************/
int TextMixer::InitMixer(int id)
{
	Log(">Init mixer [%d]\n",id);

	//Protegemos la lista
	std::unique_lock<std::mutex> lock(mutex);

	//Buscamos el text source
	TextSources::iterator it = sources.find(id);

	//Si no esta
	if (it == sources.end())
	{
		//Desprotegemos
		lock.unlock();
		//Salimos
		return Error("Mixer not found\n");
	}

	//Obtenemos el text source
	std::shared_ptr<TextSource> text = it->second;

	//INiciamos los pipes
	text->input->Init();
	text->output->Init();

	//Init worker
	text->worker->Init();

	//Set participant as reader for the worker
	text->worker->AddReader(id,text->input.get());

	Log("-Text [%d,%ls]\n",text->id,text->name.c_str());

	//Add as writter to all the other participants
	for (TextWorkers::iterator it=workers.begin();it!=workers.end();++it)
		//Add writter
		(*it)->AddWritter(text->id,text->name,text->displayName,true);

	//Set all other participants as writters for the user
	for (TextSources::iterator it=sources.begin();it!=sources.end();++it)
	{
		//Get source
		const std::shared_ptr<TextSource>& source = it->second;
		Log("[%d,%d]\n",text->id,source->id);
		//Check if it is us
		if (source->id!=text->id)
			//Add writer
			text->worker->AddWritter(source->id,source->name,source->displayName,true);
	}

	//Add the worker to the list
	workers.insert(text->worker);

	//Desprotegemos
	lock.unlock();

	Log("<Init mixer [%d]\n",id);

	//Si esta devolvemos el input
	return true;
}


/***********************
* EndMixer
*	Finaliza un text
*************************/
int TextMixer::EndMixer(int id)
{
	//Protegemos la lista
	std::unique_lock<std::mutex> lock(mutex);

	//Buscamos el text source
	TextSources::iterator it = sources.find(id);

	//Si no esta
	if (it == sources.end())
	{
		//Desprotegemos
		lock.unlock();
		//Salimos
		return false;
	}

	//Obtenemos el text source
	std::shared_ptr<TextSource> text = it->second;

	//Remove as writter to all the other participants
	for (TextWorkers::iterator it=workers.begin();it!=workers.end();++it)
		//Add writter
		(*it)->RemoveWritter(text->id);

	//Remove from the workers
	workers.erase(text->worker);

	//End the mixer
	text->worker->End();

	//Terminamos
	text->input->End();
	text->output->End();

	//Desprotegemos
	lock.unlock();

	//Si esta devolvemos el input
	return true;;
}

/***********************
* DeleteMixer
*	Borra una fuente de text
************************/
int TextMixer::DeleteMixer(int id)
{
	Log("-DeleteMixer text [%d]\n",id);

	//Protegemos la lista
	std::unique_lock<std::mutex> lock(mutex);

	//Lo buscamos
	TextSources::iterator it = sources.find(id);

	//SI no ta
	if (it == sources.end())
	{
		//DDesprotegemos la lista
		lock.unlock();
		//Salimos
		return Error("Text source not found\n");
	}

	//Obtenemos el text source
	std::shared_ptr<TextSource> text = it->second;

	//Lo quitamos de la lista
	sources.erase(it);

	//ET de la liste des workers que parcourt la passe de mixage. Sans cela, une
	//suppression sans EndMixer prealable y laissait un worker detruit : le tick
	//suivant ecrivait dedans (segfault reproduit par
	//TextMixerSite.DeleteMixerWithoutEndMixerKeepsTheOthers).
	if (text->worker)
		workers.erase(text->worker);

	//Desprotegemos la lista
	lock.unlock();

	//Plus personne ne peut obtenir de nouvelle reference : la source meurt avec
	//le dernier `shared_ptr` (ici, ou plus tard chez un appelant en vol). Ses
	//membres partent dans le bon ordre — worker d'abord, pipes ensuite.
	text.reset();

	return 0;
}

/***********************
* GetInput
*	Obtiene el input para un id
************************/
TextInput* TextMixer::GetInput(int id)
{
	//Protegemos la lista
	std::unique_lock<std::mutex> lock(mutex);

	//Buscamos el text source
	TextSources::iterator it = sources.find(id);

	//Obtenemos el input
	TextInput *input = NULL;

	//Si esta
	if (it != sources.end())
		input = (TextInput*)it->second->input.get();

	//Desprotegemos
	lock.unlock();

	//Si esta devolvemos el input
	return input;
}

/***********************
* GetSharedInput
*	Copie de shared_ptr sur le pipe d'entrée (co-propriété, Point 1 / C-4)
************************/
std::shared_ptr<TextInput> TextMixer::GetSharedInput(int id)
{
	std::unique_lock<std::mutex> lock(mutex);
	TextSources::iterator it = sources.find(id);
	std::shared_ptr<TextInput> input;
	if (it != sources.end())
		input = it->second->input;
	lock.unlock();
	return input;
}

/***********************
* GetOutput
*	Obtiene el output para un id
************************/
TextOutput* TextMixer::GetOutput(int id)
{
	//Protegemos la lista
	std::unique_lock<std::mutex> lock(mutex);

	//Buscamos el text source
	TextSources::iterator it = sources.find(id);

	//Obtenemos el output
	TextOutput *output = NULL;

	//Si esta
	if (it != sources.end())
		//Store it
		output = it->second->output.get();

	//if still not found
	if (!output)
	{
		//Check if it is private
		TextPrivates::iterator it = privates.find(id);
		//If it exist
		if (it!=privates.end())
			//Store it
			output = it->second->output.get();
	}

	//Desprotegemos
	lock.unlock();

	//Si esta devolvemos el input
	return output;
}

/***********************
* GetSharedOutput
*	Copie de shared_ptr sur le pipe de sortie (sources puis privates), Point 1
************************/
std::shared_ptr<TextOutput> TextMixer::GetSharedOutput(int id)
{
	std::unique_lock<std::mutex> lock(mutex);
	std::shared_ptr<TextOutput> output;
	//Chercher d'abord dans les sources
	TextSources::iterator it = sources.find(id);
	if (it != sources.end())
		output = it->second->output;
	//Sinon dans les privates
	if (!output)
	{
		TextPrivates::iterator itp = privates.find(id);
		if (itp != privates.end())
			output = itp->second->output;
	}
	lock.unlock();
	return output;
}


/***********************
* CreatePrivate
*	Create a private text source for one participant
************************/
int TextMixer::CreatePrivate(int id,int to,std::wstring &name)
{
	Log(">CreatePrivate text [%d,%d]\n",id,to);

	//Protegemos la lista
	std::unique_lock<std::mutex> lock(mutex);

	//Miramos que si esta
	if (privates.find(id)!=privates.end())
	{
		//Desprotegemos la lista
		lock.unlock();
		//Error
		return Error("Private sourecer already existed\n");
	}

	//Create the private text
	std::shared_ptr<TextPrivate> priv = std::make_shared<TextPrivate>();

	//Set id
	priv->id = id;
	//Set target mixer
	priv->to = to;
	//Create output (co-propriété shared_ptr, Point 1)
	priv->output = std::make_shared<PipeTextOutput>();
	//Set name
	priv->name = name;

	//Add private the list
	privates[id] = priv;

	//Desprotegemos la lista
	lock.unlock();

	//Y salimos
	Log("<CreateMixer text\n");

	return true;
}

/***********************
* InitPrivate
*	Inicializa un text
*************************/
int TextMixer::InitPrivate(int id)
{
	Log(">Init private [%d]\n",id);

	//Protegemos la lista
	//Verrou EXCLUSIF : AddWritter modifie le worker d'une source, que le thread
	//de mixage parcourt — c'etait pris ici sous le verrou lecteur, donc en
	//concurrence avec lui (TextMixerWorker n'a aucune synchronisation propre).
	std::unique_lock<std::mutex> lock(mutex);

	//Buscamos el text source
	TextPrivates::iterator it = privates.find(id);

	//Si no esta
	if (it == privates.end())
	{
		//Desprotegemos
		lock.unlock();
		//Salimos
		return Error("Mixer not found\n");
	}

	//Obtenemos el text source
	std::shared_ptr<TextPrivate> priv = it->second;

	//INiciamos los pipes
	priv->output->Init();

	//Find private target
	TextSources::iterator itSource=sources.find(priv->to);
	
	//if found
	if (itSource!=sources.end())
		//Add writer
		itSource->second->worker->AddWritter(id,priv->name,std::wstring(),false);

	//Desprotegemos
	lock.unlock();

	Log("<Init private [%d]\n",id);

	//Si esta devolvemos el input
	return true;
}


/***********************
* EndPrivate
*	Finaliza un text
*************************/
int TextMixer::EndPrivate(int id)
{
	//Protegemos la lista
	//Verrou EXCLUSIF, pour la meme raison qu'InitPrivate : RemoveWritter touche
	//le worker d'une source.
	std::unique_lock<std::mutex> lock(mutex);

	//Buscamos el text source
	TextPrivates::iterator it = privates.find(id);

	//Si no esta
	if (it == privates.end())
	{
		//Desprotegemos
		lock.unlock();
		//Salimos
		return false;
	}

	//Obtenemos el text source
	std::shared_ptr<TextPrivate> priv = it->second;

	//Find private target
	TextSources::iterator itSource=sources.find(priv->to);

	//If target found
	if (itSource!=sources.end())
		//Add writer
		itSource->second->worker->RemoveWritter(priv->id);

	//Terminamos
	priv->output->End();

	//Desprotegemos
	lock.unlock();

	//Si esta devolvemos el input
	return true;;
}

/***********************
* DeletePrivate
*	Borra una fuente de text
************************/
int TextMixer::DeletePrivate(int id)
{
	Log("-DeletePrivate text [%d]\n",id);

	//Protegemos la lista
	std::unique_lock<std::mutex> lock(mutex);

	///Buscamos el text source
	TextPrivates::iterator it = privates.find(id);

	//Si no esta
	if (it == privates.end())
	{
		//Desprotegemos
		lock.unlock();
		//Salimos
		return false;
	}

	//Obtenemos el text source
	std::shared_ptr<TextPrivate> priv = it->second;

	//Lo quitamos de la lista
	privates.erase(it);

	//Desprotegemos la lista
	lock.unlock();

	//Le canal prive meurt avec le dernier `shared_ptr` — celui-ci, ou celui d'un
	//appelant en vol ; son pipe de sortie survit tant qu'un flux le tient.
	priv.reset();

	return 0;
}
/***********************
* GetPrivateOutput
*	Obtiene el output para un id
************************/
TextOutput* TextMixer::GetPrivateOutput(int id)
{
	//Protegemos la lista
	std::unique_lock<std::mutex> lock(mutex);

	//Buscamos el text source
	TextPrivates::iterator it = privates.find(id);

	//Obtenemos el output
	TextOutput *output = NULL;

	//Si esta
	if (it != privates.end())
		output = it->second->output.get();

	//Desprotegemos
	lock.unlock();

	//Si esta devolvemos el input
	return output;
}
