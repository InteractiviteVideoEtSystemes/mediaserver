#include "AMQPcpp.h"
#include "rabbitmqserver.h"
#include "tools.h"
#include "rabbitmqmcu.h"
#include "log.h"

McuRabbitServer::McuRabbitServer(const char *cnxString, const char *pqueueName)
{
    this->cnxString = (cnxString != NULL) ? cnxString : "";
    this->pqueueName = pqueueName;
    qprivee = NULL;
    qpublique = NULL;
    cnx = NULL;
}

McuRabbitServer::~McuRabbitServer()
{
    Stop();
}

void McuRabbitServer::CreateQueues(McuRabbitHandler *evt)
{
    char privQueueName[80];
    char hostname[80];

    if ( gethostname(hostname, sizeof(hostname) ) != 0 )
    {
        strcpy(hostname, "unknown");
    }

    sprintf(privQueueName, "mcu_%s_%d", hostname, getpid() );

    qpublique = cnx->createQueue( pqueueName.c_str() );
    qpublique->Declare(AMQP_NOACK);
    qprivee = cnx->createQueue(privQueueName);
    qprivee->Declare(AMQP_NOACK | AMQP_AUTODELETE);
    AMQPExchange * ex = cnx->createExchange();
    evt->SetExchanger(ex);
    evt->SetPrivateQueue(qprivee);
    qpublique->addEvent( evt );
    qprivee->addEvent( evt );
    qprivee->Consume(AMQP_NOACK);
    qpublique->Consume(AMQP_NOACK);
    Log("-rqserver: created public queue %s, private queue %s and exchanger.\n", pqueueName.c_str(), qprivee->getName().c_str()  );
}

bool McuRabbitServer::Start(McuRabbitHandler *evt)
{
    if ( cnxString.empty() )
    {
	Log("rqserver: won't start because rabbit MQ server and credentiales are not specified.\n");
	return false;
    }

    if (! running )
    {
	running = true;
	Log("-rqserver: starting connecting to %s\n",  cnxString.c_str() );
	try
	{
	    if (cnx == NULL)
	    {
		cnx = new AMQP(cnxString.c_str());
		CreateQueues(evt);
		this->evt = evt;
		createPriorityThread(&thread,Run,this,0);
	    }
	}
        catch (AMQPException e)
        {
                std::cout << "Exception pendant la connextion: " << e.getMessage() << std::endl;
		running = false;
		if (cnx)
		{
		    delete cnx;
		    cnx = NULL;
		}
		return false;
        }
    }
    return true;
}    

bool McuRabbitServer::Stop()
{
    if (running)
    {
	void * rez;
        running = false;
        if (qpublique) qpublique->Cancel();
        pthread_join(thread, &rez);
    }
    
}

void *McuRabbitServer::Run(void *par)
{
    McuRabbitServer *inst = (McuRabbitServer *) par;
    McuRabbitHandler *evt = inst->evt;

    Log("-rqserver: ready.\n");
    while(inst->running)
    {
	try
	{
	    if (inst->cnx != NULL)
	    {
	        inst->cnx->runChannels(0);
	        cout << "Deconnexion rabbitmq ... " << endl;
	    }
        }
        catch  (AMQPException e)
	{
                std::cout << "Exception pendant le traitement: " << e.getMessage() << std::endl;
        }

	try
	{
	        cout << "Deconnexion rabbitmq ... " << endl;
		inst->qprivee->Delete();
                if (inst->cnx)
                {
                    delete inst->cnx;
                    inst->cnx = NULL;
                }
	        inst->qpublique = NULL;
	        inst->qprivee = NULL;
	        //Reconnect case
		if ( inst->running )
		{
	            cout << "reconnexion rabbitmq" << endl;
		    if ( inst->running )
	            inst->cnx = new AMQP(inst->cnxString.c_str());
	            inst->CreateQueues(evt);
		}
	}
        catch  (AMQPException e)
        {
                std::cout << "Exception pendant la reconnection : " << e.getMessage() << std::endl;
                if (inst->cnx)
                {
                    delete inst->cnx;
                    inst->cnx = NULL;
		    sleep(2);
                }
	}
    }

    Log("-rqserver: exit.\n");
    return evt;
}
