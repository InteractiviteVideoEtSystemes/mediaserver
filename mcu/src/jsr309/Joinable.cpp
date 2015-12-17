/* 
 * File:   Joinable.cpp
 * Author: Sergio
 *
 * Created on 7 de septiembre de 2011, 0:59
 */

 
#include "log.h"
#include "JSR309Manager.h"

Joinable::Joinable()
{
	sessionId		= -1;
	eventContextId 	= -1;
	jsrManager=NULL;
}

Joinable::~Joinable()
{
	sessionId		= -1;
	eventContextId 	= -1;
	jsrManager=NULL;
}


int	Joinable::SetEventHandler(int sessionId, JSR309Manager* jsrManager)
{
	this->sessionId=sessionId;
	this->jsrManager=jsrManager;
	return 1;
}

int	Joinable::SetEventContextId(int eventContextId )
{
	this->eventContextId=eventContextId;
	return 1;
}

bool Joinable::PostEvent( JSR309Event *event)
{
	if (jsrManager != NULL && sessionId >0 && eventContextId > 0)
		jsrManager->PostEvent(sessionId, eventContextId, event);

}

