#ifndef _MEDIA_H_
#define	_MEDIA_H_
#include "config.h"
#include <stdlib.h>
#include <vector>
#include <string.h>

class MediaFrame
{
public:
	class Listener
	{
	public:
		virtual void onMediaFrame(MediaFrame &frame) = 0;
	};

	class RtpPacketization
	{
	public:
        RtpPacketization( DWORD pos, DWORD size, BYTE *prefix, DWORD prefixLen )
        {
            //Store values
            this->pos = pos;
            this->size = size;
            this->prefixLen = prefixLen;
            //Check size
			if( prefixLen )
			{
				//Copy
				memcpy( this->prefix, prefix, prefixLen );
			}
        }

		DWORD GetPos()		{ return pos;	}
		DWORD GetSize()		{ return size;	}
		BYTE* GetPrefixData()	{ return prefix;	}
		DWORD GetPrefixLen()	{ return prefixLen;	}
		DWORD GetTotalLength()	{ return size+prefixLen;}
		
	private:
		DWORD	pos;
		DWORD	size;
		BYTE	prefix[16];
		DWORD	prefixLen;
	};

	typedef std::vector<RtpPacketization*> RtpPacketizationInfo;
public:
	enum Type {Audio=0,Video=1,Text=2,Application=3};
	enum MediaRole {VIDEO_MAIN=0,VIDEO_SLIDES=1 };
	
	static const char * TypeToString(Type type)
	{
		switch(type)
		{
			case Audio:
				return "Audio";
			case Video:
				return "Video";
			case Text:
				return "Text";
			case Application:
				return "Application";
			default:
				return "Unknown";
		}
	}
	
	static const char * RoleToString(MediaRole role)
	{
		switch(role)
		{
			case VIDEO_MAIN:
				return "Main";
			case VIDEO_SLIDES:
				return "Slides";
			default:
				return "Unknown";
		}
	}

	// Handle WS, TCP, RTMP and other connection full
	// media protocols. It assumes here that media connections
	// are not multiplexed accross sessions
	enum MediaProtocol
	{
	    RTP = 0,
	    RTMP = 1,
		WS = 2,
	    TCP = 3, // Used for MSRP, BFCP and other TCP based media
		UDP = 4
	};

	static const char * ProtocolToString(MediaProtocol prot)
	{
		switch(prot)
		{
			case RTP:
				return "rtp";
			case RTMP:
				return "rtmp";
			case WS:
				return "ws";
			case TCP:
				return "tcp";
			case UDP:
				return "udp";
			default:
				return "http";
		}
	}
	
	MediaFrame(Type t,DWORD size)
	{
		//Set media type
		type = t;
		//Set no timestamp
		ts = (DWORD)-1;
		//No duration
		duration = 0;
		//Set buffer size
		bufferSize = size;
		//Allocate memory
		buffer = (BYTE*) malloc(bufferSize);
		//NO length
		length = 0;
	}

	virtual ~MediaFrame()
	{
		//Clear
		ClearRTPPacketizationInfo();
		//Clear memory
		free(buffer);
	}

	void	ClearRTPPacketizationInfo()
	{
		//Emtpy
		while (!rtpInfo.empty())
		{
			//Delete
			delete(rtpInfo.back());
			//remove
			rtpInfo.pop_back();
		}
	}
	
	void	AddRtpPacket(DWORD pos,DWORD size,BYTE* prefix,DWORD prefixLen)		
	{
		rtpInfo.push_back(new RtpPacketization(pos,size,prefix,prefixLen));
	}
	
	Type	GetType()		{ return type;	}
	DWORD	GetTimeStamp()		{ return ts;	}
	DWORD	SetTimestamp(DWORD ts)	{ this->ts = ts; }

	bool	HasRtpPacketizationInfo()		{ return !rtpInfo.empty();	}
	RtpPacketizationInfo& GetRtpPacketizationInfo()	{ return rtpInfo;		}
	virtual MediaFrame* Clone() = 0;

	DWORD GetDuration()			{ return duration;		}
	void SetDuration(DWORD duration)	{ this->duration = duration;	}

	BYTE* GetData()			{ return buffer;		}
	DWORD GetLength()		{ return length;		}
	DWORD GetMaxMediaLength()	{ return bufferSize;		}

	void SetLength(DWORD length)	{ this->length = length;	}

	bool Alloc(DWORD size)
	{
		//Calculate new size
		bufferSize = size;
		//Realloc
		buffer = (BYTE*) realloc(buffer,bufferSize);
		
		return (buffer != NULL);
	}

	bool SetMedia(BYTE* data,DWORD size)
	{
		//Check size
		if (size>bufferSize)
			//Allocate new size
			Alloc(size*3/2);
		//Copy
		memcpy(buffer,data,size);
		//Increase length
		length=size;
	}

	DWORD AppendMedia(BYTE* data,DWORD size)
	{
		DWORD pos = length;
		//Check size
		if (size+length>bufferSize)
			//Allocate new size
			Alloc((size+length)*3/2);
		//Copy
		memcpy(buffer+length,data,size);
		//Increase length
		length+=size;
		//Return previous pos
		return pos;
	}
	
protected:
	Type type;
	DWORD ts;
	RtpPacketizationInfo rtpInfo;
	BYTE	*buffer;
	DWORD	length;
	DWORD	bufferSize;
	DWORD	duration;
	DWORD	clockRate;
};

struct MediaStatistics
{
	bool		isSending;
	bool		isReceiving;
	DWORD		lostRecvPackets;
	DWORD		numRecvPackets;
	DWORD		numSendPackets;
	DWORD		totalRecvBytes;
	DWORD		totalSendBytes;
        int             sendingCodec;
        int             receivingCodec;
        DWORD           bwOut;
        DWORD           bwIn;
};
#endif	/* MEDIA_H */

