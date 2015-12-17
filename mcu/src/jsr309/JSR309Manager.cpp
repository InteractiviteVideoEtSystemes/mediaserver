/* 
 * File:   JSR309Manager.cpp
 * Author: Sergio
 * 
 * Created on 8 de septiembre de 2011, 13:06
 */
#include "log.h"
#include "use.h"
#include "amf.h"
#include "JSR309Manager.h"
#include "xmlstreaminghandler.h"


JSR309Manager::JSR309Manager()
{
	//Init id
	maxId = 1;
	//NO manager
	eventMngr = NULL;
	//Create mutex
	pthread_mutex_init(&mutex,NULL);
}

JSR309Manager::~JSR309Manager()
{
	//Destroy mutex
	pthread_mutex_destroy(&mutex);
}


/**************************************
* Init
*	Inicializa la JSR309Manager
**************************************/
int JSR309Manager::Init(XmlStreamingHandler *eventMngr)
{
	timeval tv;
	
	//Bloqueamos
	pthread_mutex_lock(&mutex);

	//Estamos iniciados
	inited = true;

	//Get secs
	gettimeofday(&tv,NULL);

	//El id inicial
	maxId = (tv.tv_sec & 0x7FFF) << 16;

	//Store event mngr
	this->eventMngr = eventMngr;
		
	//Desbloqueamos
	pthread_mutex_unlock(&mutex);

	//Salimos
	return 1;
}

/**************************************
* End
*	Termina la JSR309Manager
**************************************/
int JSR309Manager::End()
{
	Log(">End JSR309Manager\n");

	//Bloqueamos
	pthread_mutex_lock(&mutex);

	//Dejamos de estar iniciados
	inited = false;

	//Paramos las conferencias
	for (MediaSessions::iterator it=sessions.begin(); it!=sessions.end(); ++it)
	{
		//Obtenemos la conferencia
		MediaSession *sess = it->second->sess;

		//End media sessions
		sess->End();

		//Delete object
		delete sess;

		//Delete also the entry
		delete (it->second);
	}

	//LImpiamos las listas
	sessions.clear();

	//NO manager
	eventMngr = NULL;
	
	//Desbloqueamos
	pthread_mutex_unlock(&mutex);

	Log("<End JSR309Manager\n");

	//Salimos
	return false;
}

int JSR309Manager::CreateEventQueue()
{
	//Check mngr
	if (!eventMngr)
		//Error
		return Error("Event manager not set!\n");

	//Create it
	return eventMngr->CreateEventQueue();
}

int JSR309Manager::DeleteEventQueue(int id)
{
	//Check mngr
	if (!eventMngr)
		//Error
		return Error("Event manager not set!\n");

	//Create it
	return eventMngr->DestroyEventQueue(id);
}


/**************************************
* CreateMediaSession
*	Inicia una conferencia
**************************************/
int JSR309Manager::CreateMediaSession(std::wstring tag,int queueId)
{
	Log(">CreateMediaSession\n");

	//Obtenemos el id
	int sessId = maxId++;

	//Creamos la multi
	MediaSession * sess = new MediaSession(tag);

	//Creamos la entrada
	MediaSessionEntry* entry = new MediaSessionEntry();

	//Guardamos los datos
	entry->id 	= sessId;
	entry->tag 	= tag;
	entry->sess 	= sess;
	entry->queueId	= queueId;
	entry->enabled 	= 1;

	//Set listener
	sess->SetListener(this,(void*)entry);

	//INit it
	sess->Init();
	//Bloqueamos
	
	pthread_mutex_lock(&mutex);

	//a�adimos a la lista
	sessions[sessId] = entry;

	//Desbloqueamos
	pthread_mutex_unlock(&mutex);

	Log("<CreateMediaSession [%d]\n",sessId);

	return sessId;
}

/**************************************
* GetMediaSessionRef
*	Obtiene una referencia a una conferencia
**************************************/
int JSR309Manager::GetMediaSessionRef(int id,MediaSession **sess)
{
	//Log(">GetMediaSessionRef [%d]\n",id);

	//Bloqueamos
	pthread_mutex_lock(&mutex);

	//Find confernce
	MediaSessions::iterator it = sessions.find(id);

	//SI no esta
	if (it==sessions.end())
	{
		//Desbloquamos el mutex
		pthread_mutex_unlock(&mutex);
		//Y salimos
		return Error("Media session not found [%d]\n",id);
	}

	//Get entry
	MediaSessionEntry *entry = it->second;

	//Check it is enabled
	if (!entry->enabled)
	{
		//Desbloquamos el mutex
		pthread_mutex_unlock(&mutex);
		//Y salimos
		return Error("MediaSession not enabled [%d]\n",id);
	}

	//Aumentamos el contador
	entry->IncUse();

	//Y obtenemos el puntero a la la conferencia
	*sess = entry->sess;

	//Desbloquamos el mutex
	pthread_mutex_unlock(&mutex);

	//Log("<GetMediaSessionRef [%d,%d]\n",entry->enabled,it->second->enabled);

	return true;
}

/**************************************
* ReleaseMediaSessionRef
*	Libera una referencia a una conferencia
**************************************/
int JSR309Manager::ReleaseMediaSessionRef(int id)
{
//	Log(">ReleaseMediaSessionRef [%d]\n",id);

	//Bloqueamos
	pthread_mutex_lock(&mutex);

	//Find confernce
	MediaSessions::iterator it = sessions.find(id);

	//SI no esta
	if (it==sessions.end())
	{
		//Desbloquamos el mutex
		pthread_mutex_unlock(&mutex);
		//Y salimos
		return Error("Media session not found [%d]\n",id);
	}

	//Get entry
	MediaSessionEntry *entry = it->second;

	//Aumentamos el contador
	entry->DecUse();

	//Desbloquamos el mutex
	pthread_mutex_unlock(&mutex);

//	Log("<ReleaseMediaSessionRef\n");

	return true;
}

/**************************************
* DeleteMediaSession
*	Inicializa la JSR309Manager
**************************************/
int JSR309Manager::DeleteMediaSession(int id)
{
	Log(">DeleteMediaSession [%d]\n",id);

	//Bloqueamos
	pthread_mutex_lock(&mutex);

	//Find conference
	MediaSessions::iterator it = sessions.find(id);

	//Check if we found it or not
	if (it==sessions.end())
	{
		//Desbloquamos el mutex
		pthread_mutex_unlock(&mutex);

		//Y salimos
		return Error("Media session not found [%d]\n",id);
	}

	//Get entry
	MediaSessionEntry *entry = it->second;

	Log("-Disabling conference [%d]\n",entry->enabled);

	//Disable it
	entry->enabled = 0;

	//Desbloquamos el mutex
	pthread_mutex_unlock(&mutex);

	//Whait to get free
	entry->WaitUnusedAndLock();

	//Get conference from ref entry
	MediaSession *sess = entry->sess;

	//If still has session
	if (sess)
	{
		//End conference
		sess->End();

		//Delete conference
		delete sess;

		//Set to null
		entry->sess = NULL;
	}

	//Desbloquamos el mutex
	entry->Unlock();

	//Bloqueamos
	pthread_mutex_lock(&mutex);

	//Find conference again
	it = sessions.find(id);

	//Check if we found it or not
	if (it==sessions.end())
	{
		//Desbloquamos el mutex
		pthread_mutex_unlock(&mutex);

		//Y salimos
		return Error("Media session not found [%d]\n",id);
	}
	
	//Delete entry
	delete (it->second);

	//Remove from list
	sessions.erase(it);

	//Desbloquamos el mutex
	pthread_mutex_unlock(&mutex);

	Log("<DeleteMediaSession [%d]\n",id);

	//Exit
	return true;
}

int JSR309Manager::PostEvent(int sessionId,int eventContextId , JSR309Event *event)
{
	Debug(">Post Event\n");
	MediaSessionEntry * entry;
	
	//Bloqueamos
	pthread_mutex_lock(&mutex);

	//Find confernce
	MediaSessions::iterator it = sessions.find(sessionId);
	
	//SI no esta
	if (it==sessions.end())
	{
		//Desbloquamos el mutex
		pthread_mutex_unlock(&mutex);
		//Y salimos
		return Error("Media session not found [%d]\n",sessionId);
	}
	
	entry = it->second;	
	JSR309EventContext* evtctx= entry->sess->GetEventContext(eventContextId);
	
	pthread_mutex_unlock(&mutex);	
	event->SetSessionTag(entry->tag);
	event->FillEvent(*evtctx);
	
	if (eventMngr )
		//Send new event
		eventMngr->AddEvent(entry->queueId,event);
		
	Debug("<PostEvent\n");
	return 1;
}

void JSR309Manager::onWebSocketConnection(const HTTPRequest &request, WebSocket *ws)
{
	::Log("JSR309Manager: incoming WebSocket connection to %s\n", request.GetRequestURI().c_str());
	MediaSession * sess;
	std::string token;
	
	int sessionId;

	// Get the URL which must look (assuming conferenceId 1234 and userId 5678) as follows:
	//   /bfcp/1234/5678
	std::string url = request.GetRequestURI();
	StringParser parser(url);

	// Check the URL.
	if (! parser.MatchString("/jsr309")) 
	{
		ws->Reject(404, "Not found");
		return;
	}

	// Extract sessionId
	if (! parser.ParseChar('/')) 
	{
		::Error("JSR309::onWebSocketConnection() | bad URL: no /jsr309/ => HTTP 400\n");
		ws->Reject(400, "Bad Request");
		return;
	}
	if (! parser.ParseInteger()) {
		::Error("jsr309::onWebSocketConnection() | bad URL: missing session ID.\n");
		ws->Reject(400, "Bad Request");
		return;
	}
	
	sessionId = (int) parser.GetIntegerValue();
	
	if (! parser.ParseChar('/'))
	{
		Error("jsr309::onWebSocketConnection() | bad URL: no no /jsr309/sessionId/ => HTTP 400\n");
		ws->Reject(400, "Bad Request no sep before token");
		return;
	}
	
	if (! parser.ParseToken())
	{
		Error("jsr309::onWebSocketConnection(): cannot textract token from URL %s\n", url.c_str());
		ws->Reject(400, "Bad Request cannot extract token");
		return;
	}
	
	token = parser.GetValue();
	if ( GetMediaSessionRef(sessionId, &sess ) )
	{
	    sess->onNewMediaConnection(ws, token);
	    ReleaseMediaSessionRef(sessionId);
	}
	else
	{
	    Error("jsr309::onWebSocketConnection() | no such session %d\n", sessionId);
	    ws->Reject(404, "No such media session");
	}
}

