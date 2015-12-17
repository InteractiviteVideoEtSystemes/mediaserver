/* 
 * File:   JSR309Manager.h
 * Author: Sergio
 *
 * Created on 8 de septiembre de 2011, 13:06
 */

#ifndef JSR309MANAGER_H
#define	JSR309MANAGER_H

#include <map>
#include "config.h"
#include "MediaSession.h"
#include "xmlstreaminghandler.h"
#include "websocketserver.h"

class JSR309Manager : 
	public MediaSession::Listener,
	public WebSocketServer::Handler
{
public:
	enum Events
	{
		PlayerEndOfFileEvent = 1,
		SipInfoFIRRequestedEvent =2
	};
public:
	JSR309Manager();
	virtual ~JSR309Manager();

	int Init(XmlStreamingHandler *eventMngr);
	int End();

	int CreateEventQueue();
	int DeleteEventQueue(int id);
	int CreateMediaSession(std::wstring tag,int queueId);
	int GetMediaSessionRef(int id,MediaSession **sess);
	int ReleaseMediaSessionRef(int id);
	int DeleteMediaSession(int id);

	int PostEvent(int sessionId,int eventContextId , JSR309Event *event);
	//Events
	//virtual void onPlayerEndOfFile(MediaSession *sess,Player *player,int playerId,void *param);
	
	// Websocket server
	virtual void onWebSocketConnection(const HTTPRequest &request, WebSocket *ws);
	
private:
	struct MediaSessionEntry : public Use
	{
		int id;
		int enabled;
		int queueId;
		std::wstring tag;
		MediaSession* sess;
	};

	typedef std::map<int,MediaSessionEntry*> MediaSessions;

private:
	XmlStreamingHandler *eventMngr;
	MediaSessions	sessions;
	pthread_mutex_t	mutex;
	int maxId;
	bool inited;
};




#endif	/* JSR309MANAGER_H */

