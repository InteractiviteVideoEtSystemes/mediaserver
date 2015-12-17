#include <pthread.h>

class AMQP;
class AMQPQueue;
class AMQPQueue::EventListener;
class McuRabbitHandler;

class McuRabbitServer
{
public:
        McuRabbitServer(const char *cnxString, const char *pqueueName);
        virtual ~McuRabbitServer();

        bool Start(McuRabbitHandler *evt = NULL);
        bool Stop();

protected:
	AMQP *cnx;
        AMQPQueue * qprivee;
        AMQPQueue * qpublique;
        pthread_t thread;
	string cnxString;
	string pqueueName;

	void CreateQueues(McuRabbitHandler * evt);

private:
	static void *Run(void *par);
	McuRabbitHandler *evt;
	bool running;
};
