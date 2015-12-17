/* 
 * File:   httpsseconnection.h
 * Author: ebuu
 * 
 * This class emulates WebSocket using a long term HTTP post
 * connection and an SSE connection.
 * 
 * Created on 23 août 2014, 18:30
 */


#ifndef HTTPSSECONNECTION_H
#define	HTTPSSECONNECTION_H

#include "websockets.h"
#include "httpparser.h"

class HttpSseConnection :
    	public WebSocket,
	public HTTPParser::Listener
{
public:
    HttpSseConnection();
    virtual ~HttpSseConnection();
    
    int Init(int fdin);
    int Associate(int fdout);
    int End();


    //Weksocket
    virtual void Accept(WebSocket::Listener *listener);
    virtual void Reject(const WORD code, const char* reason);
    virtual void SendMessage(const std::string& message);
    virtual void SendMessage(const BYTE* data, const DWORD size);
    virtual void Close();

    //HTTPParser listener
    virtual int on_url (HTTPParser*, const char *at, DWORD length);
    virtual int on_header_field (HTTPParser*, const char *at, DWORD length);
    virtual int on_header_value (HTTPParser*, const char *at, DWORD length);
    virtual int on_body (HTTPParser*, const char *at, DWORD length);
    virtual int on_message_begin (HTTPParser*);
    virtual int on_status_complete (HTTPParser*);
    virtual int on_headers_complete (HTTPParser*);
    virtual int on_message_complete (HTTPParser*);

    HTTPRequest* GetRequest() { return request; };
	
protected:
    void Start();
    void Stop();
    int Run();

private:
    static  void* run(void *par);
    void   ProcessData(BYTE *data,DWORD size);
    int    WriteData(BYTE *data,const DWORD size);
    void   SignalWriteNeeded();

private:
	int socketin;
        int socketout;
        
	pollfd ufds[2];
	bool inited;
	bool running;

	pthread_t thread;
	pthread_mutex_t mutex;

	timeval startTime;
	Listener *listener;

	DWORD recvSize;
	DWORD inBytes;
	DWORD outBytes;

	HTTPParser parser;
	HTTPRequest* request;
	HTTPResponse* response;
	std::string headerField;
	std::string headerValue;

	WebSocket::Listener *wsl;
	QWORD bandIni;
	DWORD bandSize;
	DWORD bandCalc;
};


#endif	/* HTTPSSECONNECTION_H */

