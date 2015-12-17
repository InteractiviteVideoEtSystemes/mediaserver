/*
 *  AMQP.cpp
 *  librabbitmq++
 *
 *  Created by Alexandre Kalendarev on 01.03.10.
 *
 */

#include "AMQPcpp.h"

AMQP::AMQP() {
	AMQP::init();
	AMQP::initDefault();
	AMQP::connect();
};

AMQP::AMQP(string cnnStr) {
	AMQP::init();
	AMQP::parseCnnString(cnnStr);
	AMQP::connect();
};

AMQP::~AMQP() {
	if (channels.size()) {
		vector<AMQPBase*>::iterator i;
		for (i=channels.begin(); i!=channels.end(); i++) {
			delete *i;
		}
	}

	amqp_destroy_connection(cnn);
	close(sockfd);
};

void AMQP::init() {
	exchange=NULL;
	channelNumber=0;
	sockfd = -1;
}

void AMQP::initDefault() {
	host = string(AMQPHOST);
	port = AMQPPORT;
	vhost = string(AMQPVHOST);
	user = string(AMQPLOGIN);
	password = string(AMQPPSWD);
}

void AMQP::parseCnnString( string cnnString ) {
	 if (!cnnString.size()) {
		AMQP::initDefault();
		return;
	 }

	// find '@' if Ok -> right part is host:port else all host:port
	string hostPortStr, userPswString;
	int pos = cnnString.find('@');

	switch (pos) {
		case 0:
			hostPortStr.assign(cnnString, 1, cnnString.size()-1);
			AMQP::parseHostPort(hostPortStr);
			user = string(AMQPLOGIN);
			password = string(AMQPPSWD);
		break;
		case -1:
			AMQP::parseHostPort(cnnString);
			user = string(AMQPLOGIN);
			password = string(AMQPPSWD);
		break;
		default :
			hostPortStr.assign(cnnString, pos+1, cnnString.size()-pos+1);
			userPswString.assign(cnnString, 0, pos);
			AMQP::parseHostPort(hostPortStr);
			AMQP::parseUserStr(userPswString );
		break;
	}
}

void AMQP::parseUserStr(string userString) {
	int pos = userString.find(':');
	switch (pos) {
		case 0:
			user.assign(userString, 1, userString.size()-1);
			password=AMQPPSWD;
		break;
		case -1:
			user=userString;
			password=AMQPPSWD;
		break;
		default:
			user.assign(userString, pos+1, userString.size()+1-pos);
			password.assign(userString, 0, pos);
		break;
	}
}

void AMQP::parseHostPort(string hostPortString ) {
	size_t pos = hostPortString.find(':');
	string hostString;
	string portString;

	size_t pos2 = hostPortString.find('/');

        host  = AMQPHOST;
        vhost = AMQPVHOST;
        port  = AMQPPORT;

        if (pos == string::npos) {
                if ( pos2 == string::npos) {
                        host = hostPortString;
                } else {
                        vhost.assign(hostPortString, pos2+1, hostPortString.size()-pos2);
                        if (pos2 != 0) {
                                host.assign(hostPortString, 0, pos2);
                        }
                }
        } else if (pos == 0) {
                if (pos2 == string::npos) {
                        portString.assign(hostPortString, 1, hostPortString.size()-1);
                } else {
                        portString.assign(hostPortString, 1, pos2-1);
                        vhost.assign(hostPortString, pos2+1, hostPortString.size()-pos2);
                }
                port = atoi(portString.c_str());
        } else {
                if ( pos2 == string::npos ) {
                        host.assign(hostPortString, 0, pos);
                        portString.assign(hostPortString, pos+1, hostPortString.size()-pos+1);
                } else {
                        vhost.assign(hostPortString, pos2+1, hostPortString.size()-pos2);
                        host.assign(hostPortString, 0, pos);
                        portString.assign(hostPortString, pos+1, pos2-pos-1);
                }
                port = atoi(portString.c_str());
        }
}

void AMQP::connect() {
	AMQP::sockConnect();
	AMQP::login();
}

void AMQP::printConnect() {
	 cout<<  "AMQP connection: \n";

	 cout<<  "port  = " << port << endl;
	 cout<<  "host  = " << host << endl;
	 cout<<  "vhost = " << vhost << endl;
	 cout<<  "user  = " << user << endl;
	 cout<<  "passw = " << password << endl;
}

void AMQP::sockConnect() {
	cnn = amqp_new_connection();
	sockfd = amqp_open_socket(host.c_str(), port);

	if (sockfd<0){
		amqp_destroy_connection(cnn);
		throw AMQPException("AMQP cannot create socket descriptor");
	}

	//cout << "sockfd="<< sockfd  << "  pid=" <<  getpid() <<endl;
	amqp_set_sockfd(cnn, sockfd);
}

void AMQP::login() {
	amqp_rpc_reply_t res = amqp_login(cnn, vhost.c_str(), 0, FRAME_MAX, 0, AMQP_SASL_METHOD_PLAIN, user.c_str(), password.c_str());
	if ( res.reply_type == AMQP_RESPONSE_NORMAL)
		return;

	amqp_destroy_connection(cnn);
	throw AMQPException(&res);
}

AMQPExchange * AMQP::createExchange() {
	channelNumber++;
	AMQPExchange * exchange = new AMQPExchange(&cnn,channelNumber);
	channels.push_back( dynamic_cast<AMQPBase*>(exchange) );
	return exchange;
}

AMQPExchange * AMQP::createExchange(string name) {
	channelNumber++;
	AMQPExchange * exchange = new AMQPExchange(&cnn,channelNumber,name);
	channels.push_back( dynamic_cast<AMQPBase*>(exchange) );
	return exchange;
}

AMQPQueue * AMQP::createQueue() {
	channelNumber++;
	AMQPQueue * queue = new AMQPQueue(&cnn,channelNumber);
	channels.push_back( dynamic_cast<AMQPBase*>(queue) );
	queues[channelNumber] = queue;
	return queue;
}

AMQPQueue * AMQP::createQueue(string name) {
        channelNumber++;
	AMQPQueue * queue = new AMQPQueue(&cnn,channelNumber,name);
	channels.push_back( dynamic_cast<AMQPBase*>(queue) );
	queues[channelNumber] = queue;
	return queue;
}

int AMQP::runChannels(int nbMessagesToProcess)
{
	amqp_frame_t frame;
	string queueName;
	int nbMsgProcessed = 0; 
 	AMQPQueue *q;
	map<int, AMQPQueue *>::iterator it;
	
        while ( nbMsgProcessed >=0 && ( nbMessagesToProcess == 0 || nbMsgProcessed < nbMessagesToProcess))
	{
                amqp_maybe_release_buffers(cnn);
                int result = amqp_simple_wait_frame(cnn, &frame);
		
                if (result < 0)
                {
                        cout << "Failed to read rabbitmq frame. amqp_simple_wait_frame() returned " << result << endl;
                        return -2;
                }
                //printf("frame method.id=%d  frame.frame_type=%d\n",frame.payload.method.id, frame.frame_type);

		it = queues.find(frame.channel);
		if ( it != queues.end() )
		{
		    q = it->second;
		}
		else
		{
		    q = NULL;
		}

		switch (frame.frame_type)
		{
		    case AMQP_FRAME_METHOD:
			result = processMethodFrame(q, frame);
			if ( result < 0 )
			{
			    nbMsgProcessed = result;
			}
			break;

		
		    case AMQP_FRAME_HEADER:
		    {
		        amqp_basic_properties_t * p = (amqp_basic_properties_t *) frame.payload.properties.decoded;
                        if ( q != NULL )
                        {
			    q->setHeaders(p);
			    q->prepareBuffer(frame.payload.properties.body_size);
		        }
		        else
		        {
		            cout << "AMPQ: unexpected HEADER received. Ignoring." << endl;
		        }
		        break;
		    }

		    case AMQP_FRAME_BODY:
		    {
                        if ( q != NULL )
                        {
			    result =  q->processBodyFrag(frame.payload.body_fragment.bytes, frame.payload.body_fragment.len);
			    switch(result)
			    {
			        case 0: // body incmplete. Need more data.
			            break;

			        case 1: // mody complete. Message delivered to application
			            if ( nbMessagesToProcess > 0) nbMsgProcessed++;
				    break;
				
			         case -1: // no previous DELIVERY method received.
			         case -2: // no data or buffer allocated
			             cout << "Unexpected message body received on queue " << q->name << endl;
				     break;

			        case -3: // Application requires to exit loop
			  	    nbMsgProcessed = -3; // this will do
				    break;
				
			        default:
			            break;
			    }
		        }
		        else
		        {
		            cout << "Message body received for an unknonw queue." << endl;
		        }
		        break;
		    }

		    default:
		        cout << "Received unknown frame of type " << frame.frame_type << endl;
                        break;
		}
        }
	return nbMsgProcessed;
}

int AMQP::processMethodFrame(AMQPQueue *q, amqp_frame_t frame)
{
	switch ( frame.payload.method.id )
        {
        	case AMQP_BASIC_CANCEL_OK_METHOD:
                {
                        amqp_basic_cancel_t * cancel = (amqp_basic_cancel_t *) frame.payload.method.decoded;
                        string queueName = AmqpbytesToString(cancel->consumer_tag);

                        if ( q != NULL )
                        {
                            cout << "CANCEL received on queue " << q->getName() << endl;
                            if ( q->listener )
                            {
                                q->listener->onCancel(q->pmessage);
                                amqp_maybe_release_buffers(cnn);
                                q->pmessage = NULL;
                            }
                            return -3;
                        }
                        else
                            cout << "CANCEL received on UNEXPECDED queue " << queueName << endl;
                }
                break;


                case AMQP_CHANNEL_CLOSE_METHOD:
                {
                        amqp_connection_close_t * cl = (amqp_connection_close_t *) frame.payload.method.decoded;
                        string err = "channel closed unexpectedly: " + AmqpbytesToString(cl->reply_text);
                        cout << "consume: channel " << frame.channel << " closed with error: " << cl->reply_code
                             << " - " << AmqpbytesToString(cl->reply_text) << "." << endl;
                        amqp_maybe_release_buffers(cnn);
                        throw new AMQPException(err,  cl->reply_code);
                }
                break;

                case AMQP_CONNECTION_CLOSE_OK_METHOD:
                        cout << "consume: channel " << frame.channel << " closed correctly." << endl;
                        amqp_maybe_release_buffers(cnn);
                        return -1;

                case AMQP_BASIC_DELIVER_METHOD:
                {
                        amqp_basic_deliver_t * delivery = (amqp_basic_deliver_t*) frame.payload.method.decoded;

                        string queueName = AmqpbytesToString(delivery->consumer_tag);
                        if (  q != NULL )
                        {
                            AMQPMessage *m = new AMQPMessage(q);
                            m->setConsumerTag(delivery->consumer_tag);
                            m->setDeliveryTag(delivery->delivery_tag);
                            m->setRoutingKey(delivery->routing_key);
                            q->pmessage = m;
                        }
                        else
                        {
                            cout << "received a message for an unknown queue " << queueName << ". Discarding." << endl;
                        }
                        break;
                }

		default:
			// TODO dispatch method results to pending operant in queues
			// instead of using amqpq_simple_rpc.
		        cout << "Received message method.id=" << \
                                amqp_method_name(frame.payload.method.id) << endl;
			break;
	}
	return 0;
}

void AMQP::closeChannel() {
	channelNumber--;
	AMQPBase * cnn = channels.back();
	if (cnn) {
		delete cnn;
		channels.pop_back();
	}
}
