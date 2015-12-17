#ifndef _MCU_H_
#define _MCU_H_
#include <string>
#include "pthread.h"
#include "multiconf.h"
#include "rtmpstream.h"
#include "rtmpapplication.h"
#include "xmlstreaminghandler.h"
#include "uploadhandler.h"



class MCU : 
	public RTMPApplication,
	public MultiConf::Listener,
	public UploadHandler::Listener
{
public:
	enum Events
	{
		ParticipantRequestFPU = 1,
		ParticipantRequestDocSharing = 2
	};

	struct ConferenceInfo
	{
		int id;
		std::wstring name;
		int numPart;
	};

	typedef  std::map<int,ConferenceInfo> ConferencesInfo;
public:
	MCU();
	~MCU();

	int Init(XmlStreamingHandler *eventMngr);
	int CreateEventQueue();
	int DeleteEventQueue(int id);
	int End();

	int CreateConference(std::wstring tag,int queueId);
	int GetConferenceRef(int id,MultiConf **conf);
	int GetConferenceId(const std::wstring& tag);
	int ReleaseConferenceRef(int id);
	int DeleteConference(int confId);
	int GetConferenceList(ConferencesInfo& lst);

	/** Conference events*/
	virtual void onParticipantRequestFPU(MultiConf *conf,int partId,void *param);
	virtual void onParticipantRequestDocSharing(MultiConf *conf,int partId,std::wstring status,void *param);
	
	/** RTMP application interface*/
	virtual RTMPNetConnection* Connect(const std::wstring& appName,RTMPNetConnection::Listener* listener);
	/** File uploader event */
	virtual int onFileUploaded(const char* url, const char *filename);
private:
	struct ConferenceEntry
	{
		int id;
		int numRef;
		int enabled;
		int queueId;
		MultiConf* conf;
	};

	typedef std::map<int,ConferenceEntry*> Conferences;
	typedef std::map<std::wstring,int> ConferenceTags;
private:
	XmlStreamingHandler	*eventMngr;
	Conferences		conferences;
	ConferenceTags		tags;
	int			maxId;
	pthread_mutex_t		mutex;
	int inited;
};

class PlayerRequestDocSharingEvent: public XmlEvent
{
public:
	PlayerRequestDocSharingEvent(int confId,std::wstring &tag, int partId, std::wstring &status)
	{
		//ACTIVE,WAITING_ACCEPT,NONE,FAILED
		//Get status
		UTF8Parser statusParser(status);
		
		//Get session tag
		UTF8Parser tagParser(tag);
		
		//Serialize
		DWORD len = statusParser.Serialize(this->status,64);
		
		//Serialize
		DWORD lenTag = tagParser.Serialize(this->tag,1024);

		//Set end
		this->status[len] = 0;
		this->tag[lenTag] = 0;
		
		//Store other values
		this->confId = confId;
		this->partId = partId;
	}

	virtual xmlrpc_value* GetXmlValue(xmlrpc_env *env)
	{
		return xmlrpc_build_value(env,"(iisis)",(int)MCU::ParticipantRequestDocSharing,confId,tag,partId,status);
	}
private:
	int confId;
	BYTE tag[1024];
	BYTE status[64];
	int partId;
};

class PlayerRequestFPUEvent: public XmlEvent
{
public:
	PlayerRequestFPUEvent(int confId,std::wstring &tag,int partId)
	{
		//Get session tag
		UTF8Parser tagParser(tag);
		
		//Serialize
		DWORD len = tagParser.Serialize(this->tag,1024);

		//Set end
		this->tag[len] = 0;
		//Store other values
		this->confId = confId;
		this->partId = partId;
	}

	virtual xmlrpc_value* GetXmlValue(xmlrpc_env *env)
	{
		return xmlrpc_build_value(env,"(iisi)",(int)MCU::ParticipantRequestFPU,confId,tag,partId);
	}
private:
	int confId;
	BYTE tag[1024];
	int partId;
};

#endif
