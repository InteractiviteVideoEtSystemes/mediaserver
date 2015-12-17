#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> 

#include "TCPEndpoint.h"

class TCPBuffer : TCPProtoHandler::Message
{
public:
    TCPBuffer(void *data, unsigned int size) : TCPProtoHandler::Message(data, size)
    {
	this->data = data;
	this->size = size;
    }

    void SetBuffer(void *data, unsigned int size)
    {
	this->data = data;
	this->size = size;
    }
   
    virtual const void * Serialize(unsigned int & size)
    {
	size = this->size;
	return data;
    }

private:
    void * data;
    unsigned int size;
}

TCPEndpoint::TCPEndpoint(unsigned int nbcnx)
{
    for (int i=0; i< MAX_CNX; i++)
    {
        cnxfds[i] = -1;
    }
    serverfd = -1;
    handler = NULL;
    thread = 0;
    receiving = false;
    idx = 0;
    maxcnx = nbcnx;
}

~TCPEndpoint::TCPEndpoint()
{
    End();
    if (serverfd >=0) close(serverfd);
    for (int i=0; i< MAX_CNX; i++)
    {
        if (cnxfds[i] >=0) close(cnxfds[i]);
    }
}

int TCPEndpoint::Init(int port, TCPProtoHandler * handler)
{
    sockaddr_in addr;
    this->handler = handler;
    
    if ( serverfd >= 0 )
	return Error("TCPEndpoint: already inited.\n");
	
    //Create socket
    serverfd = socket(AF_INET, SOCK_STREAM, 0);

    //Set SO_REUSEADDR on a socket to true (1):
    int optval = 1;
    setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    //Bind to first available port
    memset(&addr,0,sizeof(addr));
    addr.sin_family 	= AF_INET;
    addr.sin_addr.s_addr 	= INADDR_ANY;
    addr.sin_port 		= htons(port);

    //Bind
    if (bind(serverfd, (sockaddr *) &addr, sizeof(addr)) < 0)
	//Error
	return Error("Can't bind server socket. errno = %d.\n", errno);

    //Set non blocking so we can get an error when we are closed by end
    int fsflags = fcntl(serverfd,F_GETFL,0);
    fsflags |= O_NONBLOCK;
    fcntl(serverfd,F_SETFL,fsflags);
}

int TCPEndpoint::StartReceiving()
{    
    Log("< TCPEndpoint: starting.\n");
    if ( serverfd < 0) 
	return Error("TCPEndpoint: already inited.\n");

    

static void *TCPEndpoint::run(void *par)
{
    int i = 0;
    pollfd ufds[MAX_CNX+1];
    bool handlerReady = false;
    TCPBuffer(NULL, 0);
    
    receiving = true;
    
compute_fdset:
    ufds[i].fd = serverfd;
    ufds[i++].events  = POLLIN | POLLHUP | POLLERR ;
    
    for (int j=0; j < MAX_CNX; j++)
    {
        if ( cnxfds[j] >= 0 )
	{
	    ufds[i].fd = serverfd;
	    ufds[i].events = POLLHUP | POLLERR ;
	    if ( handlerReady ) ufds[i].events |= POLLIN;
	}
    }
    
    while (receiving)
    {
        if ( poll( ufds, i, 2000) 
	
	
	