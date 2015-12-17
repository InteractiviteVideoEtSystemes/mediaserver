#include <google/protobuf/message.h>
#include "AMQPcpp.h"

class MCU;
class MultiConf;


class McuRabbitHandler : public AMQPQueue::EventListener
{

public:
	McuRabbitHandler(MCU *mcu);
	void SetExchanger(AMQPExchange * p_ex);
	void SetPrivateQueue(AMQPQueue * q) { privateQueue = q; }
	virtual int onMessage( AMQPMessage *m);
	
protected:
	MCU *mcu;
	AMQPExchange * ex;
	AMQPQueue * privateQueue;

	class MsgHeader
	{
	public:
	    string replyTo;
	    string rqCorId;
	    string messageId;
	};

	/**
	 * Send a protocol buffer message to the reply queue
	 * indicated in the header
	 *
	 * @param h header to be sent as RQ message properties
	 * @param m protocol buffer message to send
	 **/
        int SendMessage(const MsgHeader & h, void * m);

        /**
         * Create a reply error message and send it
         * indicated in the header
         *
         * @param h header to be sent as RQ message properties
         * @param o concerned object ID
	 * @param err error code
	 * @param desc error description
         **/

        int ReplyError( const MsgHeader & h, const void * o, int err, const char * desc );

	void Dump( const char * data, size_t len);


        // Methode de traitement des messages
        int onBindReq( const char * payload, size_t len,  MsgHeader & h );
	
	/**
	 * process a CreateMcuObjectReq request to create a Conference or
	 * a participant, a sidebar or a mosaic. Cette methode ne fait
	 * pas la creation de ces objets elle-meme mais delegue a des
	 * methodes specialisees
	 * @param payload protocol buffer serialized payload containing the
	 *        CreateMcuObjectReq message
	 * @param len number of bytes of this payload
	 * @param h headers extracted from rabbitMQ enveloppe
	 **/
        int onCreateMcuObject( const char * payload, size_t len,  MsgHeader & h);

		/**
	 * process a DeleteMcuObjectReq request to delete a Conference or
	 * a participant, a sidebar or a mosaic. Cette methode ne fait
	 * pas la creation de ces objets elle-meme mais delegue a des
	 * methodes specialisees
	 * @param payload protocol buffer serialized payload containing the
	 *        CreateMcuObjectReq message
	 * @param len number of bytes of this payload
	 * @param h headers extracted from rabbitMQ enveloppe
	 **/
        int onDeleteMcuObject( const char * payload, size_t len,  MsgHeader & h);

	/**
	 * process a SetMediaPropertiesReq message to set codecs and properties
	 * attached to an existing running conference
	 * @param payload protocol buffer serialized payload containing the
	 *        CreateMcuObjectReq message
	 * @param len number of bytes of this payload
	 * @param h headers extracted from rabbitMQ enveloppe
	 **/	 
	int onSetMediaPropertiesReq( const char * payload, size_t len,  MsgHeader & h );

	/**
	 * process a StartMediaReq message to start sending or receiving medias
	 * attached to an existing running conference
	 * @param payload protocol buffer serialized payload containing the
	 *        CreateMcuObjectReq message
	 * @param len number of bytes of this payload
	 * @param h headers extracted from rabbitMQ enveloppe
	 **/	 
	int onStartMediaReq( const char * payload, size_t len,  MsgHeader & h );

	/**
	 * process a StopMediaReq message to stop sending or receiving medias
	 * attached to an existing running conference
	 * @param payload protocol buffer serialized payload containing the
	 *        CreateMcuObjectReq message
	 * @param len number of bytes of this payload
	 * @param h headers extracted from rabbitMQ enveloppe
	 **/	 
	int onStopMediaReq( const char * payload, size_t len,  MsgHeader & h );

	int onAddToMixerReq( const char * payload, size_t len,  MsgHeader & h );

private:
        int CreateConference( void *r, MsgHeader & h );
        int CreateParticipant( void *r,  MsgHeader & h );
	int StartSendingMedia( MultiConf *conf, int partId,int media, const void * m, void * r );
	int StartReceivingMedia( MultiConf *conf, int partId, int media, const void * m, void * r );
	int StopSendingMedia( MultiConf *conf, int partId, int mt, const void * m, void * r);
	int StopReceivingMedia( MultiConf *conf, int partId, int mt, const void * m, void * r);
};
