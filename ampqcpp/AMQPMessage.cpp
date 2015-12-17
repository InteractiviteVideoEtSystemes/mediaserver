/*
 *  AMQPMessage.cpp
 *  librabbitmq++
 *
 *  Created by Alexandre Kalendarev on 15.04.10.
 *
 */

#include "AMQPcpp.h"

AMQPMessage::AMQPMessage( AMQPQueue * queue )
{
	this->queue=queue;
	 message_count=-1;
	 data=NULL;
	len =0;
	maxLen = 0;
}

AMQPMessage::~AMQPMessage() {
	if (data) {
		free(data);
	}
}

void AMQPMessage::setMessage(const char * data,uint32_t length, bool copy)
{
	if (this->data)
	{
	    memset(this->data, 0, this->maxLen);
	    if ( length > maxLen )
	    {
		void *newptr = realloc(this->data, length);
		if (newptr)
		{
		    this->data = (char*) newptr;
		    this->len = maxLen = length;
		}
	    }
	    else
	    {
		this->len = length;
	    }
	}
	else
	{
	    this->data = (char*)malloc(length);
	    this->len = length;
	}

	if (!data)
		return;
	if (copy) memcpy(this->data,data,this->len);
}

int AMQPMessage::addFragment(const char * data, uint32_t offset, uint32_t length)
{
   if (this->data == NULL) return -1; // message buffer not ready

   if (data == NULL) return -2; // no data to append

   if ( offset < this->len )
   {
	uint32_t lenToCopy = min(length, this->len - offset);

	memcpy(this->data + offset, data, lenToCopy);

	if (length <  this->len - offset )
	{
	    return 0; // body not complete
	}
	else if ( length ==  this->len - offset )
	{
	    return 1; // message body complete
	}
	else
	{
	    return 2; // warning overflow
	}
   }
   return -2; // out of bounds
}

char * AMQPMessage::getMessage(uint32_t* length) {
	if (this->data)
	  {
	    *length = this->len;
	    return this->data;
	  }
	*length = 0;
	return NULL;
}

string AMQPMessage::getConsumerTag() {
	return this->consumer_tag;
}

void AMQPMessage::setConsumerTag(amqp_bytes_t consumer_tag) {
	this->consumer_tag.assign( (char*)consumer_tag.bytes, consumer_tag.len );
}

void AMQPMessage::setConsumerTag(string consumer_tag) {
	this->consumer_tag=consumer_tag;
}

void AMQPMessage::setDeliveryTag(uint32_t delivery_tag) {
	this->delivery_tag=delivery_tag;
}

uint32_t AMQPMessage::getDeliveryTag() {
	return this->delivery_tag;
}

void AMQPMessage::setMessageCount(int count) {
	this->message_count = count;
}

int AMQPMessage::getMessageCount() {
	return message_count;
}

void AMQPMessage::setExchange(amqp_bytes_t exchange) {
	if (exchange.len)
		this->exchange.assign( (char*)exchange.bytes, exchange.len );
}

void AMQPMessage::setExchange(string exchange) {
	this->exchange = exchange;
}

string AMQPMessage::getExchange() {
	return exchange;
}

void AMQPMessage::setRoutingKey(amqp_bytes_t routing_key) {
	if (routing_key.len)
		this->routing_key.assign( (char*)routing_key.bytes, routing_key.len );
}

void AMQPMessage::setRoutingKey(string routing_key) {
	this->routing_key=routing_key;
}

string AMQPMessage::getRoutingKey() {
	return routing_key;
}

void AMQPMessage::addHeader(string name, amqp_bytes_t * value) {
	string svalue;
	svalue.assign(( const char *) value->bytes, value->len);
	headers[name] = svalue;
	//headers.insert( pair<string,string>(name,svalue));
}

void AMQPMessage::addHeader(string name, uint64_t * value) {
	char ivalue[32];
	bzero(ivalue,32);
	sprintf(ivalue,"%llu", *value);
	headers[name] = string(ivalue);
	//headers.insert(pair<string,string>(name,string(ivalue)));
}

void AMQPMessage::addHeader(string name, uint8_t * value) {
	char ivalue[4];
	bzero(ivalue,4);
	sprintf(ivalue,"%d",*value);
	headers[name] = string(ivalue);
	//headers.insert( pair<string,string>(name,string(ivalue)));
}

void AMQPMessage::addHeader(amqp_bytes_t * name, amqp_bytes_t * value) {
	//cout << "name " << name << endl;
	string sname;
	sname.assign((const char *) name->bytes, name->len);
	string svalue;
	svalue.assign((const char *) value->bytes, value->len);
	headers[sname] = string(svalue);
	//headers.insert(pair<string, string>(sname, svalue));
}

string AMQPMessage::getHeader(string name) {
	if (headers.find(name) == headers.end())
		return "";
	else
		return headers[name];
}

AMQPQueue * AMQPMessage::getQueue() {
	return queue;
}
