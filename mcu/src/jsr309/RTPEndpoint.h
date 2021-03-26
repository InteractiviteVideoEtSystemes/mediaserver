/* 
 * File:   RTPEndpoint.h
 * Author: Sergio
 *
 * Created on 7 de septiembre de 2011, 12:16
 */

#ifndef RTPENDPOINT_H
#define	RTPENDPOINT_H

#include "rtpsession.h"
#include "RTPMultiplexer.h"
#include "Joinable.h"
#include "Endpoint.h"
#include "JSR309Event.h"

class RTPEndpoint :
	public RTPSession,
	public Endpoint::Port,
	public Joinable::Listener,
	public RTPSession::Listener
{
public:
	RTPEndpoint(MediaFrame::Type type, MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	virtual ~RTPEndpoint();

	virtual int Init();
	virtual int RequestUpdate();
	virtual int StartReceiving();
	virtual int StopReceiving();
	virtual int StartSending();
	virtual int StopSending();
	virtual int End();

	MediaFrame::Type GetType() { return type; }

	//Attach/Dettach to joinables
	int Attach(Joinable *join);
	int Detach();

	//Joinable interface
	virtual void Update();
	virtual void SetREMB(DWORD estimation);

	//Joinable::Listener
	virtual void onRTPPacket(RTPPacket &packet);
	virtual void onResetStream();
	virtual void onEndStream();
        virtual int  TryCheckCodec(int codec);
        
	//RTPSession::Listener
	virtual void onFPURequested(RTPSession *session);
	virtual void onReceiverEstimatedMaxBitrate(RTPSession *session,DWORD bitrate);
	virtual void onTempMaxMediaStreamBitrateRequest(RTPSession *session,DWORD bitrate,DWORD overhead);
        void SetTsTransparency(bool transparent)
	{
		tsTransparency = transparent;
	}
	
protected:
	int Run();

private:
	//Funciones propias
	static void *run(void *par);

private:
	pthread_t thread;
	DWORD codec;
	DWORD timestamp;
	DWORD freq;
	timeval prev;
	DWORD prevts;
	bool reseted;
	bool tsTransparency;
};

class ExternalFIRRequestedEvent: public JSR309Event
{
public:
	ExternalFIRRequestedEvent()
	{
	
	}
	
	virtual xmlrpc_value* GetXmlValue(xmlrpc_env *env);
	

};

#endif	/* RTPENDPOINT_H */

