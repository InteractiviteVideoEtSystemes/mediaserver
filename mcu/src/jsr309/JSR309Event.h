/* 
 * File:   JSR309EVENT.h
 * Author: Sergio
 *
 * Created on 8 de septiembre de 2011, 13:06
 */

#ifndef JSR309EVENT_H
#define	JSR309EVENT_H

#include "media.h"
#include "xmlstreaminghandler.h"

class JSR309EventContext
{
public:
	int						joinableId;
	MediaFrame::Type 		media;
	MediaFrame::MediaRole 	role;
	
	JSR309EventContext(int joinableId, MediaFrame::Type media, MediaFrame::MediaRole role);
	
	void FillEventContext(const JSR309EventContext & ctx);
};

class JSR309Event : public XmlEvent, protected JSR309EventContext
{
public:
	enum Events
	{
		PlayerEndOfFileEvent = 1,
		ExternalFIRRequestedEvent =2
	};
public:
	JSR309Event();
	virtual ~JSR309Event();

	void SetSessionTag(const std::wstring & tag) {sessionTag = tag;};
	
	void FillEvent(const JSR309EventContext & evt);
	
protected:
	int 					sessionId;
	std::wstring			sessionTag;
};





#endif	/* JSR309MANAGER_H */

