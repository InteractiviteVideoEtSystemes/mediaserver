/* 
 * File:   appmixer.cpp
 * Author: Sergio
 * 
 * Created on 15 de enero de 2013, 15:38
 */

#include "multiconf.h"
#include "shareddocmixer.h"
#include "bfcp_server.h"
#include "log.h"
#include "rtpsession.h"

#define BFCP_FLOOR_ID 1
#define MAX_RETRIES 100

SharedDocMixer::SharedDocMixer()
{
	//No output
	output  		= NULL;
	part			= NULL;
	logo			= NULL;
	bfcp_server 	= NULL;
	
	sharedMosaic	= -1;
	confId			= 0;
	//Inicializamos los mutex
	pthread_mutex_init(&mutex,NULL);
}

SharedDocMixer::~SharedDocMixer()
{
	//No output
	output  		= NULL;
	part			= NULL;
	logo			= NULL;	
	conf			= NULL;
	if ( bfcp_server != NULL )
        delete bfcp_server;
		
	bfcp_server 	= NULL;
	
	//Destroy mutex
	pthread_mutex_destroy(&mutex);
	sharedMosaic	= -1;
	confId			= 0;
}

int SharedDocMixer::Init(VideoOutput* output, Logo *logo, MultiConf* conf)
{
	//Set output
	this->output 	= output;
	this->logo 		= logo;
	this->conf		= conf;
	
	part			= NULL;
	sharedMosaic	= -1;
	confId			= 0;
	return 1;
}

int SharedDocMixer::ShareSecondaryStream(Participant *part)
{
	Log(">ShareSecondaryStream\n");

	if ( this->part != NULL )
	{
	    StopSharing();
	}
	
	if ( part != NULL  && part != this->part)
	{
	    part->SetVideoOutput(this,MediaFrame::VIDEO_SLIDES);
		conf->onRequestDocSharing(part->GetPartId(),L"ACTIVE");
	}
	this->part = part;
	return 1;
}


int SharedDocMixer::initDocSharing( Participant *part,char *sendIp,int sendPort)
{
	if (part != NULL)
	{
		switch ( part->GetDocSharingMode() )
		{
			case  Participant::BFCP_UDP:
				if (  bfcp_server)
				{
					 Log(">initDocSharing sendIp=%s , sendPort=%i\n",sendIp,sendPort);
					 bfcp_server->SendHello(part->GetPartId(),sendIp,sendPort);
				}
				
				break;
			default:
				break;
		}
	}
	return 1;
}


int SharedDocMixer::AcceptDocSharingRequest(int confId, Participant *part)
{
	int res=0;
	
	if (part != NULL)
	{
		switch ( part->GetDocSharingMode() )
		{
			case  Participant::BFCP_TCP:
			case  Participant::BFCP_UDP:
				if (  bfcp_server)
				{
					struct BFCPInfo bfcpinfo;
					if (GetBfcpInfo(part->GetPartId(),bfcpinfo) && bfcpinfo.bfcp_status == BFCP_PENDING)
					{
						res = bfcp_server->FloorRequestRespons( part->GetPartId(), part->GetPartId() , bfcpinfo.transactionId , bfcpinfo.floorRequestID , BFCP_ACCEPTED , 0 , BFCP_NORMAL_PRIORITY , false );
						if ( res )
							res = bfcp_server->FloorRequestRespons( part->GetPartId(), part->GetPartId() , bfcpinfo.transactionId ,  bfcpinfo.floorRequestID ,BFCP_GRANTED , 0 , BFCP_NORMAL_PRIORITY , false );
						bfcpinfo.bfcp_status = BFCP_GRANTED;
					}
				}
				break;
			default:
				break;
		}
		
		if ( res )
			res = ShareSecondaryStream(part);
	}
	
	return res;

}

int SharedDocMixer::RefuseDocSharingRequest(int confId, Participant *part)
{
	int res=0;
	
	if (part != NULL)
	{
		switch ( part->GetDocSharingMode() )
		{
			case  Participant::BFCP_TCP:
			case  Participant::BFCP_UDP:
				if (  bfcp_server)
				{
					struct BFCPInfo bfcpinfo ;
					if (GetBfcpInfo(part->GetPartId(),bfcpinfo) && bfcpinfo.bfcp_status == BFCP_PENDING)
					{
						res =  bfcp_server->FloorRequestRespons(part->GetPartId(), part->GetPartId() , bfcpinfo.transactionId ,  bfcpinfo.floorRequestID , BFCP_REVOKED , 0 , BFCP_NORMAL_PRIORITY , false );
						//Lock
						pthread_mutex_lock(&mutex);
						transactions.erase(part->GetPartId());
						//Unlock
						pthread_mutex_unlock(&mutex);
					}
				}
				break;
			default:
				break;
		}
	
	if (res)
		conf->onRequestDocSharing(part->GetPartId(),L"NONE");
	}	
	return res;

}


int SharedDocMixer::StopSharing()
{
	
	Participant *p = NULL;

	pthread_mutex_lock(&mutex);
	
        if ( part != NULL )
	{
		p=part;
		p->use.IncUse();
		part    = NULL;

		pthread_mutex_unlock(&mutex);

		conf->onRequestDocSharing(p->GetPartId(),L"NONE");
	
	        p->SetVideoOutput(NULL,MediaFrame::VIDEO_SLIDES);
		conf->SetDocSharingMosaic(-1);
		
		struct BFCPInfo bfcpinfo ;
		if (GetBfcpInfo(p->GetPartId(),bfcpinfo))
		{
			bfcp_server->FloorRequestRespons( p->GetPartId() ,p->GetPartId() , bfcpinfo.transactionId ,  bfcpinfo.floorRequestID, BFCP_REVOKED , 0 , BFCP_NORMAL_PRIORITY , true );
			pthread_mutex_lock(&mutex);			
			transactions.erase(p->GetPartId());
			 pthread_mutex_unlock(&mutex);
		
		}
		p->use.DecUse();

	}
	else
	{
		 pthread_mutex_unlock(&mutex);
	}
	

	if (logo != NULL && output != NULL)
	{
		//Set size
		output->SetVideoSize(logo->GetWidth(),logo->GetHeight());
		//Set image
		return output->NextFrame(logo->GetFrame());
	}
	
	
	return 1;
}

int SharedDocMixer::StopSharing(Participant *part)
{
	
	if ( this->part == part )
	{
	  
	  return StopSharing();
	}
	else	
		return 1;
}

int SharedDocMixer::NextFrame(BYTE *pic)
{
	
	if ( part != NULL && output != NULL)
	{
		
		return output->NextFrame(pic);
	}
	else
		return 0;
}

int SharedDocMixer::SetVideoSize(int width,int height) 
{
	if ( output != NULL)
	{
		return output->SetVideoSize(width,height);	
	}
	else
		return 0;
}

int SharedDocMixer::addParticipant(int confId, Participant *part,Participant::DocSharingMode docSharingMode,MediaFrame::MediaProtocol proto)
{
	int res = 1;
	if (part != NULL)
	{
		part->SetDocSharingMode(docSharingMode);
	 
		switch ( part->GetDocSharingMode() )
		{
			case  Participant::BFCP_TCP:
			case  Participant::BFCP_UDP:
				res =	StartBfcpServer(confId,part);
				if ( res  && !bfcp_server->isUserInConf(part->GetPartId()) )
					res = bfcp_server->AddUser(  part->GetPartId() );
					
				break;
			default:
				break;
		}
	}
	
	 Log(">addParticipant res=%i\n",res);
				
	 return res;
	
}

int SharedDocMixer::removeParticipant(Participant *part)
{
	int res = 0;
	if (part != NULL) 
		switch ( part->GetDocSharingMode() )
		{
			case  Participant::BFCP_TCP:
			case  Participant::BFCP_UDP:
				if ( bfcp_server != NULL && bfcp_server->isUserInConf(part->GetPartId())  )
					res = bfcp_server->RemoveUserInConf(  part->GetPartId() );
				break;
			default:
				break;
		}

	 return res;
	
}

// BFCP Implementation
bool SharedDocMixer::StartBfcpServer(int confId, Participant *part)
{
	if (!bfcp_server )
		bfcp_server = new BFCP_Server( BFCP_MAX_CONF , confId , 0 , BFCP_FLOOR_ID , 0 , this );
		
	if (bfcp_server )
    {
		this->confId = confId;
		int Port = getAvailablePort();
		
		if (! bfcp_server->isStarted() )
		{
			if (part != NULL)
			{
				
					if ( bfcp_server->OpenTcpConnection( "0.0.0.0",Port,NULL,0,BFCPConnectionRole::PASSIVE) )
					{
						Log("StartBfcpServer on TCP success \n");
					
					}
					else
					{
						Log("StartBfcpServer failed \n");
					}
			}
		}
		else
		{
			Log("StartBfcpServer already started. \n");	
		}
		
		Port = getAvailablePort();
		
		switch ( part->GetDocSharingMode() )
		{
				case  Participant::BFCP_UDP:
					
					if ( !bfcp_server->isUserInConf(part->GetPartId()) )
							bfcp_server->AddUser(  part->GetPartId() );

					
					if ( bfcp_server->OpenUdpConnection(part->GetPartId(),"0.0.0.0", Port))
					{
						Log("StartBfcpServer on UDP success \n");
					
					}
					else
					{
						Log("StartBfcpServer failed \n");
					}
					
					break;
				default:
					break;
			}
	}
	else
	{
		Log("StartBfcpServer failed to create bfcp server\n");
		return false;
	}
	
	return bfcp_server->isStarted();
    
}


bool SharedDocMixer::StopBfcpServer()
{
	bool res = true;
	BFCP_Server * b = bfcp_server;
	if (b != NULL)
    	{
		bfcp_server = NULL;
		Log("StopBfcpServer closing....\n");
		delete 	b;
	}
	return res;   
}

int SharedDocMixer::getServerPort(Participant *part)
{
	int  res = 0;
	int transport = 0;
	char localIp[BFCP_STRING_SIZE] = { 0 };
	
	if (part != NULL)
		switch ( part->GetDocSharingMode() )
		{
			case  Participant::BFCP_TCP:
				if (bfcp_server)
				{ 
					bfcp_server->GetServerInfo( localIp , &res );
				}
				break;
			case  Participant::BFCP_UDP:
			        if (bfcp_server)
				{
					bfcp_server->GetConnectionInfo(part->GetPartId(), localIp, &res, &transport);
				}
				break;
			default:
				break;
		} 
	
	return res;
		
}

int SharedDocMixer::getAvailablePort()
{
	int  port = 0;
	int retries = 0;
	bool notFound = true;
	sockaddr_in recAddr;
	int 	simSocket = FD_INVALID;
	//Clear addr
	memset(&recAddr,0,sizeof(struct sockaddr_in));

	//Set family
	recAddr.sin_family      = AF_INET;

	//Get two consecutive ramdom ports
	while (notFound && retries++< MAX_RETRIES)
	{
			//If we have a rtp socket
			if (simSocket!=FD_INVALID)
			{
					// Close first socket
					close(simSocket);
					//No socket
					simSocket = FD_INVALID;
			}
			

			//Create new sockets
			simSocket = socket(PF_INET,SOCK_STREAM,0);
			//If not forced to any port
			if (!port)
			{
					//Get random
					port = (int) (RTPSession::GetMinPort()+(RTPSession::GetMaxPort()-RTPSession::GetMinPort())*double(rand()/double(RAND_MAX)));
					//Make even
					port &= 0xFFFFFFFE;
			}
			//Try to bind to port
			recAddr.sin_port = htons(port);
			
			if(::bind(simSocket,(struct sockaddr *)&recAddr,sizeof(struct sockaddr_in))!=0)
					//Try again
					continue;
			else
				notFound =false;
	}
	
	//Close socket
	shutdown(simSocket,SHUT_RDWR);
	close(simSocket);
	//No socket
	simSocket = FD_INVALID;
	Debug("SharedDocMixer::getAvailablePort %i \n",port);	
	return port;
		
}

bool SharedDocMixer::OnBfcpServerEvent(BFCP_fsm::e_BFCP_ACT p_evt , BFCP_fsm::st_BFCP_fsm_event* p_FsmEvent )
{
  bool Status = false ;  
    if ( !p_FsmEvent ) 
        return Status ; 
        
    //Log(_T("OnBfcpServerEvent evt[%s] state[%s] \n"), getBfcpFsmAct(p_evt),getBfcpFsmAct(p_FsmEvent->State)  ); 

    switch ( p_evt )
    {
    case BFCP_fsm::BFCP_ACT_UNDEFINED :
        break ; 
    case BFCP_fsm::BFCP_ACT_INITIALIZED  :
        break ; 
    case BFCP_fsm::BFCP_ACT_DISCONNECTED  :
        // alert application only if it's client 
		
        break ; 
    case BFCP_fsm::BFCP_ACT_CONNECTED :
        // alert application only if it's client 
        break ; 
    case BFCP_fsm::BFCP_ACT_FloorRequest :
		if (conf)
		{
			if (part != NULL && part->GetPartId() == p_FsmEvent->userID)
			{
				Log("FloorRequest already processed for this user %i.\n",part->GetPartId());
			}
			else
			{
				struct BFCPInfo bfcpinfo ;
				if (part != NULL && GetBfcpInfo(part->GetPartId(),bfcpinfo))
				{
					bfcp_server->FloorRequestRespons( part->GetPartId(), part->GetPartId() , bfcpinfo.transactionId ,  bfcpinfo.floorRequestID, BFCP_REVOKED , 0 , BFCP_NORMAL_PRIORITY , false );
					
					//Lock
					pthread_mutex_lock(&mutex);
					transactions.erase(part->GetPartId());
					//Unlock
					pthread_mutex_unlock(&mutex);
					
				}
				
				bfcpinfo.floorRequestID = p_FsmEvent->FloorRequestID;
				bfcpinfo.transactionId 	= p_FsmEvent->TransactionID;
				bfcpinfo.bfcp_status	= BFCP_PENDING;
				
				//Lock
				pthread_mutex_lock(&mutex);
				transactions[p_FsmEvent->userID] = bfcpinfo;
				//Unlock
				pthread_mutex_unlock(&mutex);
				
				conf->onRequestDocSharing(p_FsmEvent->userID ,L"WAITING_ACCEPT");
			}
		}	
		break ; 
    case BFCP_fsm::BFCP_ACT_FloorRelease  :
        if ( bfcp_server )
		{
            Status = bfcp_server->SendFloorStatus( 0 , 0 , 0 , NULL , true ) ; 
			if (part != NULL && part->GetPartId() == p_FsmEvent->userID)
			{
				
				StopSharing(part);
				//Lock
				pthread_mutex_lock(&mutex);
				transactions.erase(p_FsmEvent->userID);
				//Unlock
				pthread_mutex_unlock(&mutex);
			}
        }
        break ; 
    case BFCP_fsm::BFCP_ACT_FloorRequestQuery :
        break ; 
    case BFCP_fsm::BFCP_ACT_UserQuery  :
        break ; 
    case BFCP_fsm::BFCP_ACT_FloorQuery  :
        break ; 
    case BFCP_fsm::BFCP_ACT_ChairAction  :
        break ; 
    case BFCP_fsm::BFCP_ACT_Hello :
        break ; 
    case BFCP_fsm::BFCP_ACT_Error :
        break ; 
    case BFCP_fsm::BFCP_ACT_FloorRequestStatusAccepted :
        break ; 
    case BFCP_fsm::BFCP_ACT_FloorRequestStatusAborted :
        break ; 
    case BFCP_fsm::BFCP_ACT_FloorRequestStatusGranted  :
        break ; 
    case BFCP_fsm::BFCP_ACT_UserStatus :
        break ; 
    case BFCP_fsm::BFCP_ACT_FloorStatusAccepted :
        break ; 
    case BFCP_fsm::BFCP_ACT_FloorStatusAborted :
        break ; 
    case BFCP_fsm::BFCP_ACT_FloorStatusGranted  :
        break ; 
    case BFCP_fsm::BFCP_ACT_ChairActionAck :
        break ; 
    case BFCP_fsm::BFCP_ACT_HelloAck  :
        if ( bfcp_server ){
			if (sharedMosaic >= 0)
			{
				if (conf)
					conf->SetDocSharingMosaic(sharedMosaic,p_FsmEvent->userID);
				Status = bfcp_server->SendFloorStatus( p_FsmEvent->userID , p_FsmEvent->BeneficiaryID , 0 , NULL , false ) ; 
			}
				
        }
        break ; 
    case BFCP_fsm::BFCP_ACT_SHARE :
        break ; 
    case BFCP_fsm::BFCP_ACT_SERVER_SHARE :
        break ; 
    default :
        break ; 
    }
    return Status ;
} 


void SharedDocMixer::SetSharedMosaic(int mosaicId)
{
	Log(">SetSharedMosaic\n");
	if (part)
	{
		bfcp_server->SendFloorStatus( part->GetPartId() , 0 , 0 , NULL , true ) ; 
		sharedMosaic = mosaicId;
	}
}

bool SharedDocMixer::GetBfcpInfo(int partId,struct SharedDocMixer::BFCPInfo &info)
{
	//Lock
	pthread_mutex_lock(&mutex);
						
	//Find transaction
	Transactions::iterator it = transactions.find(partId);
	
	//Unlock
	pthread_mutex_unlock(&mutex);
	//If not found
	
	if (it == transactions.end())
		return false;
	info =it->second;
	//Get the transactionId
	return true;

}




int SharedDocMixer::End()
{

	StopSharing();
	
		//Reset output
	output 		= NULL;
	logo		= NULL;
	conf    	= NULL;
	
	if ( bfcp_server != NULL )
        delete bfcp_server;
		
    bfcp_server = NULL;
	confId			= 0;
	sharedMosaic	= -1;
	
	return 1;
	
};
