#ifndef WSENDPOINT_H
#define	WSENDPOINT_H

#include "RTPMultiplexer.h"
#include "Joinable.h"
#include "websocketserver.h"
#include "websockets.h"
#include "Endpoint.h"
#include "codecs.h"
#include "text.h"
#include "redcodec.h"

class WSEndpoint : 
	public Endpoint::Port,
	public Joinable::Listener,
	public WebSocket::Listener,
	public TextOutput
{
public :
	WSEndpoint(MediaFrame::Type type);
	
	virtual ~WSEndpoint();

	// Port interface
	virtual int Init() {};
	virtual int End();

	//Joinable interface
	virtual void Update() {};
	virtual void SetREMB(DWORD estimation) {};

	//Joinable::Listener
	virtual void onRTPPacket(RTPPacket &packet);
	virtual void onResetStream() { SendReplacementChar(true); };
	virtual void onEndStream() { SendReplacementChar(true); };

	//Websocket::Listener
	virtual void onOpen(WebSocket *ws);
	virtual void onMessageStart(WebSocket *ws,const WebSocket::MessageType type,const DWORD length) ;
	virtual void onMessageData(WebSocket *ws,const BYTE* data, const DWORD size);
	virtual void onMessageEnd(WebSocket *ws);
	virtual void onError(WebSocket *ws);
	virtual void onClose(WebSocket *ws);
		
	static void SetLocalPort(int port);	
	static void SetLocalHost(char* host);	
	
	int  	GetLocalPort();
	char*  	GetLocalHost();
	
	void SetUseRed(bool red){useRed = red;};
	void SetPrimaryPayloadType(BYTE pt){payloadType = pt;};
	
	virtual int SendFrame(TextFrame &frame);
	
private:
	WebSocket::MessageType msgType;
	MediaFrame * media;
	
	// to generate timestamp
	timeval clock;
	Joinable *joined;
	
	WebSocket * _ws;
	bool		useRed;	
	static int  	wsPort;
	static char* 	wsHost;
	BYTE		payloadType;
	
	RedundentCodec* RedCodec;
	WORD pseudoSeqNum; 
	WORD pseudoSeqCycle; 
	
	void SendReplacementChar(bool toWsSide);
	void PacketToWs(TextFrame & frame);	
};
#endif	/* WSENDPOINT_H */
