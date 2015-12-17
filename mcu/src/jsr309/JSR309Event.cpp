/* 
 * File:   JSR309EVENT.cpp
 * Author: Sergio
 *
 * Created on 8 de septiembre de 2011, 13:06
 */
#include "JSR309Event.h"
#include "log.h"
 
JSR309EventContext::JSR309EventContext(int joinableId, MediaFrame::Type media, MediaFrame::MediaRole role)
{
		this->joinableId = joinableId;
		this->media		= media;
		this->role		= role;
}

void JSR309EventContext::FillEventContext(const JSR309EventContext & ctx)
{
		this->joinableId = ctx.joinableId;
		this->media		= ctx.media;
		this->role		= ctx.role;
}


JSR309Event::JSR309Event() : JSR309EventContext(0,(MediaFrame::Type) 0, (MediaFrame::MediaRole) 0)
{
	sessionId=-1;
}

JSR309Event::~JSR309Event()
{

}

void JSR309Event::FillEvent(const JSR309EventContext & evt)
{
	FillEventContext(evt);
}


