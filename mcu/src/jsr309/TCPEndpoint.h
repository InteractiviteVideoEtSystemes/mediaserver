/* 
 * File:   TCPEndpoint.h
 * Author: Emmanuel VUU
 *
 * Created on Feb 9, 2014
 */

#ifndef TCPENDPOINT_H
#define	TCPENDPOINT_H

/**
 * This interface defines the methods called by TCPENdPoint to handle and process
 * incoming messages.
 */
class TCPProtoHandler
{
public:
    enum Proto { BFCP, MSRP, UNKNOWN };

    class Message
    {
    public:
        Message(void *data, unsigned int size) {};
        virtual const void * Serialize(unsigned int & size) = 0;
    }
    
    /** 
     * called when a complete message is received
     * @param fdidx: index of file descriptor
     * @param data: bytes of message
     * @param size: size of message
     */
    virtual int onMessage(int fdidx, Message & m) = 0;
    
    virtual int onConnect(int fdidx) = 0;
    /** 
     * called one connection
     * @param fdidx: index of file descriptor
     * @param hasonecnx: true if EndPoint has at lease one active TCP connection.
     */
    virtual int onDisconnect(int fdidx, bool hasonecnx) = 0;
    
    virtual bool IsSupported(Proto proto) { return false; }
}

/**
 * This class provide a TCP server and manage one (in the future several) inbould connection.
 * and passes recieved messages and event to its handler (if defined).
 **/
 
class TCPEndpoint : TCPProtoHandler
{
public:
	static const int MAX_CNX = 5;
	
	virtual ~TCPEndpoint();

	int Init(int port, TCPProtoHandler * handler);
	int StartReceiving();
	int End();
	
	virtual int onMessage(int fdidx, void * data, unsigned int size);
        virtual int onConnect(int fdidx);
        virtual int onDisconnect(int fdidx)

	int GetPort();
	
protected:
	TCPEndpoint(unsigned int nbcnx = 1);
	virtual int onRawData(int fdidx, void * data, unsigned int size);
	
private:
	static void *run(void *par);

	pthread_t thread;
	bool receiving;
	int serverfd;
	int cnxfds[MAX_CNX];
	int idx;
	int maxcnx;

protected:
	TCPProtoHandler * handler
};

TCPEndpoint * CreateTCPEndPoint(TCPProtoHandler::Proto proto);

#endif	/* RTPENDPOINT_H */

