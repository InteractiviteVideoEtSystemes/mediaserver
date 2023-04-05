#include "log.h"
#include "xmlrpcserver.h"
#include "xmlhandler.h"
#include "xmlstreaminghandler.h"
#include "websockets.h"
#include "statushandler.h"
#include "mcustatushandler.h"
#include "audiomixer.h"
#include "rtmpserver.h"
#include "mcu.h"
#include "broadcaster.h"
#include "mediagateway.h"
#include "jsr309/JSR309Manager.h"
#include "websocketserver.h"
#include "websockets.h"
#include "jsr309/WSEndpoint.h"
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "amf.h"
#include "dtls.h"
#include <openssl/crypto.h>

#ifdef MOTELI
#include "moteli/rabbitmqmcu.h"
#include "moteli/rabbitmqserver.h"
#endif

extern "C" {
#include "libavcodec/avcodec.h"
}
extern XmlHandlerCmd mcuCmdList[];
extern XmlHandlerCmd broadcasterCmdList[];
extern XmlHandlerCmd mediagatewayCmdList[];
extern XmlHandlerCmd jsr309CmdList[];

void log_ffmpeg(void* ptr, int level, const char* fmt, va_list vl)
{
	static int print_prefix = 1;
	char line[1024];


#ifndef MCUDEBUG
	if (level > AV_LOG_ERROR)
		return;
#endif

	//Format the
	av_log_format_line(ptr, level, fmt, vl, line, sizeof(line), &print_prefix);

	//Remove buffer errors
	if (strstr(line,"vbv buffer overflow")!=NULL)
		//exit
		return;
	//Log
	Log(line);
}

int lock_ffmpeg(void **param, enum AVLockOp op)
{
	//Get mutex pointer
	pthread_mutex_t* mutex = (pthread_mutex_t*)(*param);
	//Depending on the operation
	switch(op)
	 {
		case AV_LOCK_CREATE:
			//Create mutex
			mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
			//Init it
			pthread_mutex_init(mutex,NULL);
			//Store it
			*param = mutex;
			break;
		case AV_LOCK_OBTAIN:
			//Lock
			pthread_mutex_lock(mutex);
			break;
		case AV_LOCK_RELEASE:
			//Unlock
			pthread_mutex_unlock(mutex);
			break;
		case AV_LOCK_DESTROY:
			//Destroy mutex
			pthread_mutex_destroy(mutex);
			//Free memory
			free(mutex);
			//Clean
			*param = NULL;
			break;
	}
	return 0;
}

static XmlRpcServer * gserver = NULL;
static XmlStreamingHandler * geventHandlers[4] = { NULL, NULL, NULL, NULL };


void signing_handler(int sig)
{
    for (int i=0; i<4; i++)
    {
        if ( geventHandlers[i] != NULL) geventHandlers[i]->DestroyAllQueues();
    }
    Log("Stopping mediaserver ....\n");
    if (gserver) gserver->Stop();
}


static pthread_mutex_t *lockarray;


static void lock_callback(int mode, int type, char *file, int line)
{
  (void)file;
  (void)line;
  if (mode & CRYPTO_LOCK) {
    pthread_mutex_lock(&(lockarray[type]));
  }
  else {
    pthread_mutex_unlock(&(lockarray[type]));
  }
}

static unsigned long thread_id(void)
{
  unsigned long ret;

  ret=(unsigned long)pthread_self();
  return(ret);
}

static void init_locks(void)
{
  int i;

  lockarray=(pthread_mutex_t *)OPENSSL_malloc(CRYPTO_num_locks() *
                                        sizeof(pthread_mutex_t));
  for (i=0; i<CRYPTO_num_locks(); i++) {
    pthread_mutex_init(&(lockarray[i]),NULL);
  }

  CRYPTO_set_id_callback((unsigned long (*)())thread_id);
  CRYPTO_set_locking_callback((void (*)(int, int, const char*, int))lock_callback);
}

static void kill_locks(void)
{
  int i;

  CRYPTO_set_locking_callback(NULL);
  for (i=0; i<CRYPTO_num_locks(); i++)
    pthread_mutex_destroy(&(lockarray[i]));

  OPENSSL_free(lockarray);
}

int main(int argc,char **argv)
{
	//Init random
	srand(time(NULL));

	//Init open ssl lib
    OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
	init_locks();

	//Set default values
	bool forking = false;
	int port = 8080;
	char* iface = NULL;
	int   wsPort = 9090;
	char* wsHost = NULL;
	int rtmpPort = 1935;
	int minPort = RTPSession::GetMinPort();
	int maxPort = RTPSession::GetMaxPort();
	int vadPeriod = 5000;
	const char *logfile = "mcu.log";
	const char *pidfile = "mcu.pid";
#ifdef MOTELI
	const char *queueName = "mcu1";
	const char *cnxString = NULL;
#endif
	const char *crtfile = "/etc/mediaserver/mcu.crt";
	const char *keyfile = "/etc/mediaserver/mcu.key";

	//Get all
	for(int i=1;i<argc;i++)
	{
		//Check options
		if (strcmp(argv[i],"-h")==0 || strcmp(argv[i],"--help")==0)
		{
			//Show usage
			printf("Mediaserver version %s %s\r\n", MCUVERSION, MCUDATE);
			printf("Usage: mcu [-h] [--help] [--mcu-log logfile] [--mcu-pid pidfile] [--http-port port] [--rtmp-port port] [--min-rtp-port port] [--max-rtp-port port] [--vad-period ms]\r\n\r\n"
				"Options:\r\n"
				" -h,--help        Print help\r\n"
				" -f               Run as daemon in safe mode\r\n"
				" -d               Enable debug logging\r\n"
				" --mcu-log        Set mcu log file path (default: mcu.log)\r\n"
				" --mcu-pid        Set mcu pid file path (default: mcu.pid)\r\n"
				" --http-port      Set HTTP xmlrpc api port\r\n"
				" --min-rtp-port   Set min rtp port\r\n"
				" --max-rtp-port   Set max rtp port\r\n"
				" --rtmp-port      Set RTMP port\r\n"
				" --websocket-port Set WebSocket server port \r\n"
				" --vad-period     Set the VAD based conference change period in milliseconds\r\n");
#ifdef MOTELI
			printf("usage for rabbit MQ connector:\r\n"
			       "mcu --rq-queue queueName --rq-host user:passwd@host:port/vhost\n"
			       "Option:\n"
			       " --rq-queue	name of public rabbit MQ to bind to.\n"
			       " --rq-host	user:passwd user and password to connect to Rabbit MQ.\n"
			       "		host: DNS name of system running rabbit MQ brooker.\n"
			       "		vhost: virtual host to use.\n");
#endif 
				
			//Exit
			return 0;
		} else if (strcmp(argv[i],"-f")==0)
			//Fork
			forking = true;
		else if (strcmp(argv[i],"--http-port")==0 && (i+1<argc))
			//Get port
			port = atoi(argv[++i]);
		else if (strcmp(argv[i],"-d")==0)
			//Enable debug
			Logger::EnableDebug(true);
		else if (strcmp(argv[i],"--rtmp-port")==0 && (i+1<argc))
			//Get rtmp port
			rtmpPort = atoi(argv[++i]);
		else if (strcmp(argv[i],"--websocket-port")==0 && (i+1<argc))
			//Get port
			wsPort = atoi(argv[++i]);
		else if (strcmp(argv[i],"--min-rtp-port")==0 && (i+1<argc))
			//Get rtmp port
			minPort = atoi(argv[++i]);
		else if (strcmp(argv[i],"--max-rtp-port")==0 && (i+1<argc))
			//Get rtmp port
			maxPort = atoi(argv[++i]);
		else if (strcmp(argv[i],"--mcu-log")==0 && (i+1<argc))
			//Get rtmp port
			logfile = argv[++i];
		else if (strcmp(argv[i],"--mcu-pid")==0 && (i+1<argc))
			//Get rtmp port
			pidfile = argv[++i];
		else if (strcmp(argv[i],"--vad-period")==0 && (i+1<=argc))
			//Get rtmp port
			vadPeriod = atoi(argv[++i]);
		else if (strcmp(argv[i],"--websocket-host")==0 && (i+1<argc))
			//Get host
			wsHost = argv[++i];
#ifdef MOTELI
		else if (strcmp(argv[i],"--rq-queue")==0 && (i+1<=argc))		
			queueName = argv[++i];

		else if (strcmp(argv[i],"--rq-host")==0 && (i+1<=argc))		
			cnxString = argv[++i];
#endif /* MOTELI */
	}
	
	//Loop
	while(forking)
	{
		//Create the chld
		pid_t pid = fork();
		// fork error
		if (pid<0) exit(1);
		// parent exits
		if (pid>0) exit(0);

		//Log
		printf("MCU started\r\n");
		
		//Create the safe child
		pid = fork();

		//Check pid
		if (pid==0)
		{
			//It is the child obtain a new process group
			setsid();
			//for each descriptor opened
			for (int i=getdtablesize();i>=0;--i)
				//Close it
				close(i);
			//Redirect stdout and stderr
			int fd = open(logfile, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
			dup(fd);
			dup2(1,2);
			close(fd);
			//And continule
			break;
		} else if (pid<0)
			//Error
			return 0;

		//Pid string
		char spid[16];
		//Print it
		sprintf(spid,"%d",pid);

		//Write pid to file
		int pfd = open(pidfile, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
		//Write it
		write(pfd,spid,strlen(spid));
		//Close it
		close(pfd);

		int status;

		do
		{
			//Wait for child
			if (waitpid(pid, &status, WUNTRACED | WCONTINUED)<0)
				return -1;
			//If it has exited or stopped
			if (WIFEXITED(status) || WIFSTOPPED(status))
				//Exit
				return 0;
			//If we have been  killed
			if (WIFSIGNALED(status) && WTERMSIG(status)==9)
				//Exit
				return 0;
		} while (!WIFEXITED(status) && !WIFSIGNALED(status));
	}

	//Dump core on fault
	rlimit l = {RLIM_INFINITY,RLIM_INFINITY};
	//Set new limit
        setrlimit(RLIMIT_CORE, &l);

	//Register mutext for ffmpeg
	av_lockmgr_register(lock_ffmpeg);

	//Set log level
	av_log_set_callback(log_ffmpeg);

	//Ignore SIGPIPE
	signal( SIGPIPE, SIG_IGN );
	signal( SIGINT, signing_handler );
	signal( SIGTERM, signing_handler );
	//Hack to allocate fd =0 and avoid bug closure
	int fdzero = socket(AF_INET, SOCK_STREAM, 0);

	//Create servers
	XmlRpcServer	server(port);
	RTMPServer	rtmpServer;
	WebSocketServer wsServer;

	//Log version
	Log("-MCU Version %s %s\r\n", MCUVERSION, MCUDATE);
        gserver = &server;



	//Set DTLS certificate
	DTLSConnection::SetCertificate(crtfile,keyfile);
	//Log
	Log("-Set SSL certificate files [crt:\"%s\",key:\"%s\"]\n",crtfile,keyfile);

	//Init DTLS
	if (DTLSConnection::ClassInit()) {
	//Print hashes
		Log("-DTLS SHA1   local fingerprint \"%s\"\n",DTLSConnection::GetCertificateFingerPrint(DTLSConnection::SHA1).c_str());
		Log("-DTLS SHA256 local fingerprint \"%s\"\n",DTLSConnection::GetCertificateFingerPrint(DTLSConnection::SHA256).c_str());
	}
	// DTLS not available.
	else {
		Error("DTLS initialization failed, no DTLS available\n");
	}
	//
	//Create services
	MCU		mcu;
	Broadcaster	broadcaster;
	MediaGateway	mediaGateway;
	JSR309Manager	jsr309Manager;

#ifdef MOTELI
	McuRabbitServer rqServer(cnxString, queueName);
	McuRabbitHandler rqHandler(&mcu);
#endif

	//Create xml cmd handlers for the mcu and broadcaster
	XmlHandler xmlrpcmcu(mcuCmdList,(void*)&mcu);
	XmlHandler xmlrpcbroadcaster(broadcasterCmdList,(void*)&broadcaster);
	XmlHandler xmlrpcmediagateway(mediagatewayCmdList,(void*)&mediaGateway);
	XmlHandler xmlrpcjsr309(jsr309CmdList,(void*)&jsr309Manager);

	//Create upload handlers
	UploadHandler uploadermcu(&mcu);

	McuStatusHandler mcustatus(&mcu);

	//Create http streaming for service events
	XmlStreamingHandler xmleventjsr309;
	XmlStreamingHandler xmleventmcu;
	XmlStreamingHandler xmleventmediaGateway;

	geventHandlers[0] = &xmleventjsr309;
	geventHandlers[1] = &xmleventmcu;
	geventHandlers[2] = &xmleventmediaGateway;
	
	//And default status hanlder
	StatusHandler status;

	//Init de mcu
	mcu.Init(&xmleventmcu);
	//Init the broadcaster
	broadcaster.Init();
	//Init the media gateway
	mediaGateway.Init(&xmleventmediaGateway);
	
	//INit the jsr309
	jsr309Manager.Init(&xmleventjsr309);

	//Add the rtmp application from the mcu to the rtmp server
	rtmpServer.AddApplication(L"mcu/",&mcu);
	rtmpServer.AddApplication(L"mcutag/",&mcu);
	//Add the rtmp applications from the broadcaster to the rmtp server
	rtmpServer.AddApplication(L"broadcaster/publish",&broadcaster);
	rtmpServer.AddApplication(L"broadcaster",&broadcaster);
	rtmpServer.AddApplication(L"streamer/mp4",&broadcaster);
	rtmpServer.AddApplication(L"streamer/flv",&broadcaster);
	//Add the rtmp applications from the media gateway
	rtmpServer.AddApplication(L"bridge/",&mediaGateway);
	
	//Append mcu cmd handler to the http server
	server.AddHandler("/mcu",&xmlrpcmcu);
	server.AddHandler("/broadcaster",&xmlrpcbroadcaster);
	server.AddHandler("/mediagateway",&xmlrpcmediagateway);
	server.AddHandler("/jsr309",&xmlrpcjsr309);
	server.AddHandler("/events/jsr309",&xmleventjsr309);
	server.AddHandler("/events/mcu",&xmleventmcu);
	server.AddHandler("/events/mediagateway",&xmleventmediaGateway);

	//Add uploaders
	server.AddHandler("/upload/mcu/app/",&uploadermcu);
	
	//Add websocket handlers
	wsServer.AddHandler("/jsr309", &jsr309Manager );
	//Add the html status handler
	server.AddHandler("/status/general",&status);
        server.AddHandler("/status/mcu",&mcustatus);
	//Init the rtmp server
	if (! rtmpServer.Init(rtmpPort)) goto server_init_failed;

	//Set port ramge
	if (!RTPSession::SetPortRange(minPort,maxPort))
		//Using default ones
		Log("-RTPSession using default port range [%d,%d]\n",RTPSession::GetMinPort(),RTPSession::GetMaxPort());
	
	//Set default video mixer vad period
	VideoMixer::SetVADDefaultChangePeriod(vadPeriod);
	//Init web socket server
	if ( ! wsServer.Init(wsPort) ) goto server_init_failed;
	
	if (wsHost)
		WSEndpoint::SetLocalHost(wsHost);
	
	WSEndpoint::SetLocalPort(wsPort);

#ifdef MOTELI
	if(cnxString) rqServer.Start(&rqHandler);
#endif
	//Run it
	server.Start();

server_init_failed:
	wsServer.End();
	//End the rtmp server
	rtmpServer.End();
#ifdef MOTELI
	rqServer.Stop();
#endif
	//End the mcu
	mcu.End();
	//End the broadcaster
	broadcaster.End();
	//End the media gateway
	mediaGateway.End();
	//End the jsr309
	jsr309Manager.End();
kill_locks();
}

