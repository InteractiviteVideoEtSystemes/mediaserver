#ifndef _XMLHANDLER_H_
#define _XMLHANDLER_H_
#include <xmlrpc.h>
#include "xmlrpcserver.h"
#include "config.h"
#include "media.h"

xmlrpc_value* xmlerror (xmlrpc_env *env,const char *msg);
xmlrpc_value* xmlok (xmlrpc_env *env,xmlrpc_value *array=NULL);
int xmlparsemap(xmlrpc_env *env, xmlrpc_value *map, Properties & props);
xmlrpc_value* xmlserialize(xmlrpc_env *env, const char * media, const struct MediaStatistics * stats);

struct XmlHandlerCmd
{
        const char * name;
        xmlrpc_value* (*func) (xmlrpc_env *env, xmlrpc_value *param_array, void *user_data);
};

class XmlHandler :
	public Handler
{
public:
	XmlHandler();
	XmlHandler(XmlHandlerCmd *cmd,void *user_data);
	~XmlHandler();
	virtual int AddMethod(const char *name,xmlrpc_method method,void *user_data);
	virtual int ProcessRequest(TRequestInfo *req,TSession * const ses);
private:
	xmlrpc_registry *registry;
};

#endif
