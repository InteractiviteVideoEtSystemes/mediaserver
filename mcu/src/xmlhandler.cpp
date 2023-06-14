#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <log.h>
#include "xmlhandler.h"
#include "tools.h"

xmlrpc_value *xmlerror( xmlrpc_env *env, const char *msg )
{
    //Send error
    Error( "Processing xml cmd [\"%s\"]\n", msg );
    //Exit
    return xmlrpc_build_value( env, "{s:i,s:s}", "returnCode", 0, "errorMsg", msg );
}

xmlrpc_value *xmlok( xmlrpc_env *env, xmlrpc_value *array )
{
    //Si es null ponemos uno vacio
    if( array == NULL )
        array = xmlrpc_build_value( env, "()" );

    //Creamos el valor de retorno
    xmlrpc_value *ret = xmlrpc_build_value( env, "{s:i,s:A}", "returnCode", 1, "returnVal", array );

    //Decrementamos el contador
    xmlrpc_DECREF( array );

    //Y salimos
    return ret;
}

int xmlparsemap(xmlrpc_env *env, xmlrpc_value *map, Properties & props)
{
	xmlrpc_value *key, *val;
	const char *strKey;
	const char *strVal;
	int sz = xmlrpc_struct_size(env,map);

	for (int i=0;i<sz;i++)
	{
		xmlrpc_struct_read_member(env,map,i,&key,&val);
		xmlrpc_parse_value(env,key,"s",&strKey);
		xmlrpc_parse_value(env,val,"s",&strVal);
		props[strKey] = strVal;
		xmlrpc_DECREF(key);
		xmlrpc_DECREF(val);
	}
	return sz;
}


xmlrpc_value* xmlserialize(xmlrpc_env *env, const char * media, const struct MediaStatistics * stats)
{
    return xmlrpc_build_value(env,"(siiiiiiiiiii)",
                        media,
                        stats->isReceiving,stats->isSending,
                        stats->lostRecvPackets,
                        stats->numRecvPackets, stats->numSendPackets,
                        stats->totalRecvBytes,stats->totalSendBytes,
                        stats->bwOut, stats->bwIn,
                        stats->sendingCodec, stats->receivingCodec);
}

/**************************************
* XmlHandler
*	Constructor
*************************************/
XmlHandler::XmlHandler()
{
	xmlrpc_env env;

	//Build the enviroment
	xmlrpc_env_init(&env);

	//Creamos el registro
	registry = xmlrpc_registry_new(&env);
}

/**************************************
* XmlHandler
*	Constructor
*************************************/
XmlHandler::XmlHandler(XmlHandlerCmd *cmd,void *user_data)
{
	xmlrpc_env env;

	//Build the enviroment
	xmlrpc_env_init(&env);

	//Creamos el registro
	registry = xmlrpc_registry_new(&env);

	//Append commands
        while (cmd && cmd->name)
	{
		//Append methods
		AddMethod(cmd->name,cmd->func,user_data);
		//Next
		cmd++;
	}
}
XmlHandler::~XmlHandler()
{
	//Destroy
	xmlrpc_registry_free(registry);
}
/**************************************
* XmlHandler
*	A�ade un metodo
*************************************/
int XmlHandler::AddMethod(const char *name,xmlrpc_method method,void *user_data)
{
	xmlrpc_env env;

	//Build the enviroment
	xmlrpc_env_init(&env);

	//Add Methods
	xmlrpc_registry_add_method(&env, registry, NULL, name, method, user_data);
 	//xmlrpc_registry_add_method_w_doc( &env, registry, NULL, method_name, method, user_data, signature, help);

	//Salimos
	return !env.fault_occurred;
}

/**************************************
* ProcessRequest
*	Procesa una peticion
*************************************/
int XmlHandler::ProcessRequest(TRequestInfo *req,TSession * const ses)
{
	xmlrpc_env env;
	int inputLen;
	char *method;
	xmlrpc_value *params = NULL;
	timeval tv;

	Log(">ProcessRequest [uri:%s]\n",req->uri);

	//Init timer
	getUpdDifTime(&tv);

	//Creamos un enviroment
	xmlrpc_env_init(&env);

	//Si no es post
	if (req->method != m_post)
		//Mandamos error
		return XmlRpcServer::SendError(ses, 405, "Only POST allowed");

	//Obtenemos el content type
	const char * content_type = RequestHeaderValue(ses, (char*)"content-type");

	//Si no es el bueno
	 if (content_type == NULL || strcmp(content_type, "text/xml") != 0)
		return XmlRpcServer::SendError(ses, 400, "Wrong content-type");

	//Obtenemos el content length
	const char * content_length = RequestHeaderValue(ses, (char*)"content-length");

	//Si no hay 
	if (content_length == NULL)
		return XmlRpcServer::SendError(ses,411,"No content-length");

	//Obtenemos el entero
	inputLen = atoi(content_length);

	//Tiene que ser mayor que cero
	if ((inputLen < 0) || (inputLen > xmlrpc_limit_get(XMLRPC_XML_SIZE_LIMIT_ID)))
		return XmlRpcServer::SendError(ses,400,"Size limit");

	//Creamos un buffer para el body
	char * buffer = (char *) malloc(inputLen);

	if (!XmlRpcServer::GetBody(ses,buffer,inputLen))
	{
		//LIberamos el buffer
		free(buffer);

		//Y salimos sin devolver nada
		Log("Operation timedout\n");
		return 1;
	}

	//Get method name
	xmlrpc_parse_call(&env,buffer,inputLen,(const char**)&method,&params);

	Log("-ProcessRequest [method:%s]\n",method);

	//Free name and params
	free(method);
	xmlrpc_DECREF(params);

	//Generamos la respuesta
	xmlrpc_mem_block *output = xmlrpc_registry_process_call(
			&env, 
			registry, 
			NULL,
			buffer,
			inputLen
		);

	//Si todo ha ido bien
	if (!env.fault_occurred)
	{
		//POnemos el content type
		ResponseContentType(ses, (char*)"text/xml; charset=\"utf-8\"");
		//Y mandamos la respuesta
		XmlRpcServer::SendResponse(ses,200,XMLRPC_MEMBLOCK_CONTENTS(char, output), XMLRPC_MEMBLOCK_SIZE(char, output));
	} else
		XmlRpcServer::SendError(ses,500);

	//Liberamos
	XMLRPC_MEMBLOCK_FREE(char, output);

	//Liberamos el buffer
	free(buffer);

	Log("<ProcessRequest [time:%llu]\n",getDifTime(&tv)/1000);

	return TRUE;
}

