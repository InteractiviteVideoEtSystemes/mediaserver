/* 
 * File:   Joinable.h
 * Author: Sergio
 *
 * Created on 7 de septiembre de 2011, 0:59
 */

#ifndef JOINABLE_H
#define	JOINABLE_H
#include "rtp.h"
#include "xmlstreaminghandler.h"
#include "JSR309Event.h"

class JSR309Manager;

class Joinable
{
public:

	Joinable();
	~Joinable();
	
	class Listener 
	{
	public:
		//Virtual desctructor
		virtual ~Listener(){};
	public:
		//Interface
		virtual void onRTPPacket(RTPPacket &packet) = 0;
		virtual void onResetStream() = 0;
		virtual void onEndStream() = 0;
        virtual int  TryCheckCodec(int codec) { return codec; }
	};
	
public:
	int	SetEventHandler(int sessionId,JSR309Manager* jsrManager);
	int	SetEventContextId(int eventContextId );
	int	GetEventContextId() {return eventContextId;};
	
	virtual void AddListener(Listener *listener) = 0;
	virtual void Update() = 0;
	virtual void SetREMB(DWORD estimation) = 0;
	virtual void RemoveListener(Listener *listener) = 0;
protected:
	bool PostEvent( JSR309Event *event);
private:
	int sessionId;
	int eventContextId;
	JSR309Manager* jsrManager;
};


#endif	/* JOINABLE_H */

