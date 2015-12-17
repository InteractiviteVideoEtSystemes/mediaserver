/* 
 * File:   mediagateway.h
 * Author: Sergio
 *
 * Created on 22 de diciembre de 2010, 18:10
 */

#ifndef _MEDIAGATEWAY_H_
#define	_MEDIAGATEWAY_H_
#include <map>
#include <string>
#include "pthread.h"
#include "mediabridgesession.h"
#include "rtmpstream.h"
#include "rtmpapplication.h"
#include "xmlstreaminghandler.h"

class MediaGateway : public RTMPApplication
{
public:
	MediaGateway();
	~MediaGateway();

	bool Init(XmlStreamingHandler *p_eventMngr);
	DWORD CreateMediaBridge(const std::wstring &name);
	bool  SetMediaBridgeInputToken(DWORD id,const std::wstring &token);
	bool  SetMediaBridgeOutputToken(DWORD id,const std::wstring &token);
	DWORD GetMediaBridgeIdFromInputToken(const std::wstring &token);
	DWORD GetMediaBridgeIdFromOutputToken(const std::wstring &token);
	bool  GetMediaBridgeRef(DWORD id,MediaBridgeSession **session);
	bool  ReleaseMediaBridgeRef(DWORD id);
	bool  DeleteMediaBridge(DWORD confId);
	int CreateEventQueue();
	int DeleteEventQueue(int id);
	XmlStreamingHandler* getEventMngr();
	int getQueueId();
	void setQueueId(int p_queueId);
	bool End();

	/** RTMP application interface*/
	virtual RTMPNetConnection* Connect(const std::wstring& appName,RTMPNetConnection::Listener* listener);
protected:

	struct MediaBridgeEntry
	{
		DWORD id;
		DWORD numRef;
		DWORD enabled;
		std::wstring name;
		MediaBridgeSession* session;
	};
	typedef std::map<DWORD,MediaBridgeEntry> MediaBridgeEntries;
private:
	XmlStreamingHandler	*eventMngr;
	int 				queueId;
	MediaBridgeEntries		bridges;
	DWORD			maxId;
	pthread_mutex_t		mutex;
	bool			inited;
};


#endif	/* MEDIAGATEWAY_H */

