/* 
 * File:   dtmfmessage.cpp
 * Author: Sergio
 * 
 * Created on 6 de noviembre de 2012, 15:55
 */

#include <sys/socket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "log.h"
#include "tools.h"
#include "dtmfmessage.h"

DTMFMessage::DTMFMessage( BYTE* data, int size, int type, bool mark, unsigned int event ,	unsigned int event_end ,	unsigned int samples )
{
	//Store values
	this->event 	= 	event;
	this->event_end = 	event_end;
	this->samples 	= 	samples;
	this->data		=	data;
	this->size  	=	size;
	this->type		=	type;
	this->mark 		= 	mark;

}

DTMFMessage::~DTMFMessage()
{
	
}

DTMFMessage* DTMFMessage::Parse(RTPPacket* packet)
{
			unsigned int event;
			unsigned int event_end;
			unsigned int samples;
			
			char resp = 0;

			/* Figure out event, event end, and samples */
			event = ntohl(*((unsigned int *)(packet->GetMediaData())));
			event >>= 24;
			event_end = ntohl(*((unsigned int *)(packet->GetMediaData())));
			event_end <<= 8;
			event_end >>= 24;
			samples = ntohl(*((unsigned int *)(packet->GetMediaData())));
			samples &= 0xFFFF;

			
			/* Figure out what digit was pressed */
			if (event < 10) {
				resp = '0' + event;
			} else if (event < 11) {
				resp = '*';
			} else if (event < 12) {
				resp = '#';
			} else if (event < 16) {
				resp = 'A' + (event - 12);
			} else if (event < 17) {	/* Event 16: Hook flash */
				resp = 'X';
			} else {
				/* Not a supported event */
				Log("Ignoring RTP 2833 Event: %08x. Not a DTMF Digit.\n", event);
				return NULL;
			}
			Log("- RTP RFC2833 Event: %08x (end = %x, len = %d)\n", event, event_end & 0x80, packet->GetMediaLength());
			
			DTMFMessage* msg = new DTMFMessage(packet->GetMediaData(),packet->GetMediaLength(),packet->GetType(),packet->GetMark(), event,event_end,samples);
			
			return msg;
}


