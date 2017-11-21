/* 
 * File:  dtmfmessage.h
 * Author: Thomas
 *
 */

#ifndef DTMFMESSAGE_H
#define	DTMFMESSAGE_H
#include "config.h"
#include "tools.h"
#include "rtp.h"

class DTMFMessage
{
public:
	DTMFMessage(BYTE* data, int size,int type, bool mark, unsigned int event,	unsigned int event_end,	unsigned int samples );
	~DTMFMessage();
	static DTMFMessage* Parse(RTPPacket * packet);

public:

	unsigned int event;
	unsigned int event_end;
	unsigned int samples;
	BYTE* 	data;
	int 	size;
	int 	type;
	bool 	mark;
};

#endif	/* DTMFMESSAGE_H */

