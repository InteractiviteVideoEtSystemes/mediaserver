#include <string>
#include <vector>
#include <string.h>

#include "xmlhandler.h"
#include "mcu.h"

//CreateConference
xmlrpc_value* CreateConference(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;
	UTF8Parser tagParser;
	 //Parseamos
	char *str;
	int vad = 0;

	int rate = 16000;
	int queueId = 0;
	
	xmlrpc_parse_value(env, param_array, "(siii)", &str, &vad, &rate, &queueId);
	//Comprobamos si ha habido error
	if(env->fault_occurred)
	{
		xmlrpc_env_clean(env);
		xmlrpc_env_init(env);
		// Try with one less param (older API version)
		xmlrpc_parse_value(env, param_array, "(sii)", &str, &vad, &queueId);
		rate = 16000;
		if(env->fault_occurred)
		{
			//Send errro
			return xmlerror(env,"Fault occurred: bad argument");
		}
	}
	

	//Parse string
	tagParser.Parse((BYTE*)str,strlen(str));

	//Creamos la conferencia
	int confId = mcu->CreateConference(tagParser.GetWString(),queueId);

	//Si error
	if (!confId>0)
		return xmlerror(env,"Error creating conference");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference deleted before initing");

	//La iniciamos
	int res = conf->Init(vad,rate);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Could not create conference");

	//Devolvemos el resultado
	return xmlok(env,xmlrpc_build_value(env,"(i)",confId));
}

xmlrpc_value* UpdateConference(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
    	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;
	int vad = 0;
	int rate = 0;

	
	 //Parseamos
	int confId;
	xmlrpc_parse_value(env, param_array, "(iii)", &confId, &vad, &rate);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	if (vad >=0 && vad <= 2) conf->SetVADMode(vad);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Devolvemos el resultado
	return xmlok(env);
}
xmlrpc_value* DeleteConference(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	xmlrpc_parse_value(env, param_array, "(i)", &confId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Delete conference 
	if (!mcu->DeleteConference(confId))
		return xmlerror(env,"Conference does not exist");

	//Devolvemos el resultado
	return xmlok(env);
}



xmlrpc_value* GetConferences(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MCU::ConferencesInfo list;

	//Obtenemos la referencia
	if(!mcu->GetConferenceList(list))
		return xmlerror(env,"Could not retreive conference info list");

	//Create array
	xmlrpc_value* arr = xmlrpc_array_new(env);

	//Process result
	for (MCU::ConferencesInfo::iterator it = list.begin(); it!=list.end(); ++it)
	{
		//Create array
		xmlrpc_value* val = xmlrpc_build_value(env,"(isi)",it->second.id,it->second.name.c_str(),it->second.numPart);
		//Add it
		xmlrpc_array_append_item(env,arr,val);
		//Release
		xmlrpc_DECREF(val);
	}

	//return
	return xmlok(env,arr);
}

xmlrpc_value* CreateMosaic(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int comp;
	int size;
	xmlrpc_parse_value(env, param_array, "(iii)", &confId,&comp,&size);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");
	 
	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int mosaicId = conf->CreateMosaic((Mosaic::Type)comp,size);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!mosaicId)
		return xmlerror(env,"Could not create mosaic");

	//Devolvemos el resultado
	return xmlok(env,xmlrpc_build_value(env,"(i)",mosaicId));
}

xmlrpc_value* SetMosaicOverlayImage(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int mosaicId;
	char *filename;
	xmlrpc_parse_value(env, param_array, "(iis)", &confId,&mosaicId,&filename);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->SetMosaicOverlayImage(mosaicId,filename);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Could set overlay image");

	//Devolvemos el resultado
	return xmlok(env,xmlrpc_build_value(env,"(i)",mosaicId));
}

xmlrpc_value* ResetMosaicOverlay(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int mosaicId;
	xmlrpc_parse_value(env, param_array, "(ii)", &confId,&mosaicId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->ResetMosaicOverlay(mosaicId);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Could reset overlay image");

	//Devolvemos el resultado
	return xmlok(env,xmlrpc_build_value(env,"(i)",mosaicId));
}
xmlrpc_value* DeleteMosaic(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int mosaicId;
	xmlrpc_parse_value(env, param_array, "(ii)", &confId,&mosaicId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->DeleteMosaic(mosaicId);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Could delete mosaic");

	//Devolvemos el resultado
	return xmlok(env,xmlrpc_build_value(env,"(i)",mosaicId));
}
xmlrpc_value* CreateSidebar(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	xmlrpc_parse_value(env, param_array, "(i)", &confId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int sidebarId = conf->CreateSidebar();

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!sidebarId)
		return xmlerror(env,"Could not create sidebar");

	//Devolvemos el resultado
	return xmlok(env,xmlrpc_build_value(env,"(i)",sidebarId));
}

xmlrpc_value* DeleteSidebar(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int sidebarId;
	xmlrpc_parse_value(env, param_array, "(ii)", &confId,&sidebarId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->DeleteSidebar(sidebarId);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Could delete sidebar");

	//Devolvemos el resultado
	return xmlok(env,xmlrpc_build_value(env,"(i)",sidebarId));
}

xmlrpc_value* CreateParticipant(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;
	UTF8Parser parser;

	 //Parseamos
	int confId;
	int mosaicId;
	int sidebarId;
	char *name;
	int type;
	xmlrpc_parse_value(env, param_array, "(isiii)", &confId,&name,&type,&mosaicId,&sidebarId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");
	 
	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//Parse string
	parser.Parse((BYTE*)name,strlen(name));

	//La borramos
	int partId = conf->CreateParticipant(mosaicId,sidebarId,parser.GetWString(),(Participant::Type)type);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!partId)
		return xmlerror(env,"Error creating participant");

	//Devolvemos el resultado
	return xmlok(env,xmlrpc_build_value(env,"(i)",partId));
}

xmlrpc_value* DeleteParticipant(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int partId;
	xmlrpc_parse_value(env, param_array, "(ii)", &confId,&partId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");
	 
	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->DeleteParticipant(partId);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error deleting participant");

	//Devolvemos el resultado
	return xmlok(env);
}
xmlrpc_value* AddConferenceToken(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	UTF8Parser parser;
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	char *str;
	xmlrpc_parse_value(env, param_array, "(is)", &confId, &str);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Parse string
	parser.Parse((BYTE*)str,strlen(str));

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	bool res  = conf->AddBroadcastToken(parser.GetWString());

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Could not add pin to broadcast session");

	//Devolvemos el resultado
	return xmlok(env);
}
xmlrpc_value* AddParticipantInputToken(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	UTF8Parser parser;
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int partId;
	char *str;
	xmlrpc_parse_value(env, param_array, "(iis)", &confId, &partId, &str);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Parse string
	parser.Parse((BYTE*)str,strlen(str));

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	bool res  = conf->AddParticipantInputToken(partId,parser.GetWString());

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Could not add pin to participant input");

	//Devolvemos el resultado
	return xmlok(env);
}
xmlrpc_value* AddParticipantOutputToken(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	UTF8Parser parser;
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int partId;
	char *str;
	xmlrpc_parse_value(env, param_array, "(iis)", &confId, &partId, &str);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Parse string
	parser.Parse((BYTE*)str,strlen(str));

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	bool res  = conf->AddParticipantOutputToken(partId,parser.GetWString());

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Could not add pin to participant output");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* StartBroadcaster(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId, mosaicId, sidebarId;
	xmlrpc_parse_value(env, param_array, "(iii)", &confId, &mosaicId, &sidebarId);

	//Comprobamos si ha habido error
	if (env->fault_occurred)
	{
		xmlrpc_env_clean(env);
        xmlrpc_env_init(env);

	    mosaicId = 0;
	    sidebarId = 0;
	    xmlrpc_parse_value(env, param_array, "(i)", &confId);
	    if (env->fault_occurred)
		xmlerror(env,"Fault occurred");
	}
	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int port = conf->StartBroadcaster(mosaicId,sidebarId);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!port)
		return xmlerror(env,"Could not start broadcaster");

	//Devolvemos el resultado
	return xmlok(env,xmlrpc_build_value(env,"(i)",port));
}

xmlrpc_value* StopBroadcaster(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	xmlrpc_parse_value(env, param_array, "(i)", &confId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");
	 
	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->StopBroadcaster();
	
	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error stoping broadcaster");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* StartPublishing(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	char *server;
	int port;
	char* app;
	char* stream;
	xmlrpc_parse_value(env, param_array, "(isiss)", &confId,&server,&port,&app,&stream);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//Publish it
	int id = conf->StartPublishing(server,port,app,stream);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!id)
		return xmlerror(env,"Could not start publisihing broadcast");

	//Devolvemos el resultado
	return xmlok(env,xmlrpc_build_value(env,"(i)",id));
}

xmlrpc_value* StopPublishing(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int id;
	xmlrpc_parse_value(env, param_array, "(ii)", &confId,&id);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//Stop publishing
	int res = conf->StopPublishing(id);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error stoping publishing broadcaster");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* SetVideoCodec(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	//Parseamos
	int confId;
	int partId;
	int codec;
	int mode;
	int fps;
	int bitrate;
	int quality;
	int fillLevel;
	int intraPeriod;
	int role;
	Properties properties;
	xmlrpc_value *map;
	xmlrpc_parse_value(env, param_array, "(iiiiiiiSi)", &confId,&partId,&codec,&mode,&fps,&bitrate,&intraPeriod,&map,&role);
	
	if (env->fault_occurred)
	{
	    // Try without role argument
	    xmlrpc_env_clean(env);
	    xmlrpc_env_init(env);
	    role = MediaFrame::VIDEO_MAIN;
	   xmlrpc_parse_value(env, param_array, "(iiiiiiiS)", &confId,&partId,&codec,&mode,&fps,&bitrate,&intraPeriod,&map);
	
	}
	
	//Check if it is new api
	if(!env->fault_occurred)
	{
		//Get map size
		int j = xmlrpc_struct_size(env,map);

		//Parse rtp map
		for (int i=0;i<j;i++)
		{
			xmlrpc_value *key, *val;
			const char *strKey;
			const char *strVal;
			//Read member
			xmlrpc_struct_read_member(env,map,i,&key,&val);
			//Read name
			xmlrpc_parse_value(env,key,"s",&strKey);
			//Read value
			xmlrpc_parse_value(env,val,"s",&strVal);
			//Add to map
			properties[strKey] = strVal;
			//Decrement ref counter
			xmlrpc_DECREF(key);
			xmlrpc_DECREF(val);
		}
	} else {
		//Clean error
		xmlrpc_env_clean(env);
		xmlrpc_env_init(env);
		//Parse old values,
		xmlrpc_parse_value(env, param_array, "(iiiiiiiii)", &confId,&partId,&codec,&mode,&fps,&bitrate,&quality,&fillLevel,&intraPeriod);
	}

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");
	 
	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->SetVideoCodec(partId,codec,mode,fps,bitrate,intraPeriod,properties,(MediaFrame::MediaRole)role);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Video codec not supported.");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* SetAudioCodec(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int partId;
	int codec;
	xmlrpc_value *map;
	Properties properties;

	xmlrpc_parse_value(env, param_array, "(iiiS)", &confId,&partId,&codec,&map);
	if (env->fault_occurred)
	{
		xmlrpc_env_clean(env);
		xmlrpc_env_init(env);
		// Support for older interface wtihout properties
		xmlrpc_parse_value(env, param_array, "(iii)", &confId,&partId,&codec);
		if (env->fault_occurred)
		{
			return xmlerror(env,"Fault occurred. Invalid arguments");
		}
	}
	else
	{
		//Get map size
		int j = xmlrpc_struct_size(env,map);

		//Parse rtp map
		for (int i=0;i<j;i++)
		{
			xmlrpc_value *key, *val;
			const char *strKey;
			const char *strVal;
			//Read member
			xmlrpc_struct_read_member(env,map,i,&key,&val);
			//Read name
			xmlrpc_parse_value(env,key,"s",&strKey);
			//Read value
			xmlrpc_parse_value(env,val,"s",&strVal);
			//Add to map
			properties[strKey] = strVal;
			//Decrement ref counter
			xmlrpc_DECREF(key);
			xmlrpc_DECREF(val);
		}	
	}
	
	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->SetAudioCodec(partId,codec, properties);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Audio codec not supported.");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* SetTextCodec(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int partId;
	int codec;

	xmlrpc_parse_value(env, param_array, "(iii)", &confId,&partId,&codec);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->SetTextCodec(partId,codec);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Text codec not supported.");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* SetAppCodec(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int partId;
	int codec;

	xmlrpc_parse_value(env, param_array, "(iii)", &confId,&partId,&codec);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->SetAppCodec(confId, partId,codec);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"app codec not supported.");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* SetCompositionType(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int mosaicId;
	int comp;
	int size;
	xmlrpc_parse_value(env, param_array, "(iiii)", &confId,&mosaicId,&comp,&size);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");
	 
	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->SetCompositionType(mosaicId,(Mosaic::Type)comp,size);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* SetParticipantMosaic(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int partId;
	int mosaicId;
	xmlrpc_parse_value(env, param_array, "(iii)", &confId,&partId,&mosaicId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->SetParticipantMosaic(partId,mosaicId);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* SetParticipantSidebar(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int partId;
	int sidebarId;
	xmlrpc_parse_value(env, param_array, "(iii)", &confId,&partId,&sidebarId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->SetParticipantSidebar(partId,sidebarId);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* SetMosaicSlot(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int mosaicId;
	int num;
	int id;
	xmlrpc_parse_value(env, param_array, "(iiii)", &confId,&mosaicId,&num,&id);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");
	 
	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//Set slot
	int res = conf->SetMosaicSlot(mosaicId,num,id);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* AddMosaicParticipant(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int mosaicId;
	int partId;
	xmlrpc_parse_value(env, param_array, "(iii)", &confId,&mosaicId,&partId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//Set slot
	int res = conf->AddMosaicParticipant(mosaicId,partId);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* RemoveMosaicParticipant(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int mosaicId;
	int partId;
	xmlrpc_parse_value(env, param_array, "(iii)", &confId,&mosaicId,&partId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//Set slot
	int res = conf->RemoveMosaicParticipant(mosaicId,partId);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(res < 0)
		return xmlerror(env,"Error: no such participant");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* AddSidebarParticipant(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int sidebarId;
	int partId;
	xmlrpc_parse_value(env, param_array, "(iii)", &confId,&sidebarId,&partId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//Set slot
	int res = conf->AddSidebarParticipant(sidebarId,partId);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* RemoveSidebarParticipant(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int sidebarId;
	int partId;
	xmlrpc_parse_value(env, param_array, "(iii)", &confId,&sidebarId,&partId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//Set slot
	int res = conf->RemoveSidebarParticipant(sidebarId,partId);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* CreatePlayer(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;
	UTF8Parser parser;
	
	 //Parseamos
	int confId;
	int privateId;
	char *name;
	xmlrpc_parse_value(env, param_array, "(iis)", &confId,&privateId,&name);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//Parse string
	parser.Parse((BYTE*)name,strlen(name));

	//La borramos
	int playerId = conf->CreatePlayer(privateId,parser.GetWString());

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!playerId)
		return xmlerror(env,"Could not create player in conference");

	//Devolvemos el resultado
	return xmlok(env,xmlrpc_build_value(env,"(i)",playerId));
}

xmlrpc_value* DeletePlayer(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int playerId;
	xmlrpc_parse_value(env, param_array, "(ii)", &confId,&playerId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->DeletePlayer(playerId);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Could not delete player from conference");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* StartPlaying(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int playerId;
	char *filename;
	int loop;
	xmlrpc_parse_value(env, param_array, "(iisi)", &confId, &playerId, &filename, &loop);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->StartPlaying(playerId,filename,loop);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Could not start playback");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* StopPlaying(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int playerId;
	xmlrpc_parse_value(env, param_array, "(ii)", &confId,&playerId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->StopPlaying(playerId);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Could not stop play back in conference");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* StartRecordingParticipant(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int playerId;
	char *filename;
	xmlrpc_parse_value(env, param_array, "(iis)", &confId, &playerId, &filename);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->StartRecordingParticipant(playerId,filename);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Could not start playback");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* StopRecordingParticipant(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int playerId;
	xmlrpc_parse_value(env, param_array, "(ii)", &confId,&playerId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->StopRecordingParticipant(playerId);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Could not stop play back in conference");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* StartRecordingBroadcaster(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	char *filename;
	int mosaicId;
	int sidebarId;
	
	xmlrpc_parse_value(env, param_array, "(isii)", &confId, &filename,&mosaicId, &sidebarId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->StartRecordingBroadcaster(filename,mosaicId,sidebarId);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Could not record broadcast");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* StopRecordingBroadcaster(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL; 

	 //Parseamos
	int confId;
	xmlrpc_parse_value(env, param_array, "(i)", &confId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->StopRecordingBroadcaster();

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Could not stop recording in conference");

	//Devolvemos el resultado
	return xmlok(env);
}
xmlrpc_value* SendFPU(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int partId;
	xmlrpc_parse_value(env, param_array, "(ii)", &confId, &partId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->SendFPU(partId);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Could not start playback");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* SetMute(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int partId;
	int media;
	int isMuted;
	xmlrpc_parse_value(env, param_array, "(iiii)", &confId, &partId, &media, &isMuted);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->SetMute(partId,(MediaFrame::Type)media,isMuted);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Could not start playback");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* GetParticipantStatistics(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int partId;
	xmlrpc_parse_value(env, param_array, "(ii)", &confId, &partId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//Get statistics
	MultiConf::ParticipantStatistics *partStats = conf->GetParticipantStatistic(partId);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!partStats)
		return xmlerror(env,"Participant not found");

	//Create array
	xmlrpc_value* arr = xmlrpc_array_new(env);
	
	//Process result
	for (MultiConf::ParticipantStatistics::iterator it = partStats->begin(); it!=partStats->end(); ++it)
	{
		//Get media
		std::string media = it->first;
		//Get stats
		MediaStatistics stats = it->second;
		//Create array
		xmlrpc_value* val = xmlrpc_build_value(env,"(siiiiiii)",media.c_str(),stats.isReceiving,stats.isSending,stats.lostRecvPackets,stats.numRecvPackets,stats.numSendPackets,stats.totalRecvBytes,stats.totalSendBytes);
		//Add it
		xmlrpc_array_append_item(env,arr,val);
		//Release
		xmlrpc_DECREF(val);
	}

        delete partStats;
	//return
	return xmlok(env,arr);
}


xmlrpc_value* GetMosaicPositions(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;
	std::list<int> positions;

	 //Parseamos
	int confId;
	int mosaicId;
	xmlrpc_parse_value(env, param_array, "(ii)", &confId, &mosaicId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//Get statistics
	conf->GetMosaicPositions(mosaicId,positions);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Create array
	xmlrpc_value* arr = xmlrpc_array_new(env);

	//Process result
	for (std::list<int>::iterator it = positions.begin(); it!=positions.end(); ++it)
	{
		//Create array
		xmlrpc_value* val = xmlrpc_build_value(env,"i",*it);
		//Add it
		xmlrpc_array_append_item(env,arr,val);
		//Release
		xmlrpc_DECREF(val);
	}

	//return
	return xmlok(env,arr);
}
xmlrpc_value* MCUEventQueueCreate(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
        MCU *mcu = (MCU *)user_data;

	//Create the event queue
	int queueId = mcu->CreateEventQueue();

	//Si error
	if (!queueId>0)
		return xmlerror(env,"Could not create event queue");

	//Devolvemos el resultado
	return xmlok(env,xmlrpc_build_value(env,"(i)",queueId));
}

xmlrpc_value* MCUEventQueueDelete(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
        MCU *mcu = (MCU *)user_data;

	 //Parseamos
	int queueId;
	xmlrpc_parse_value(env, param_array, "(i)", &queueId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Delete event queue
	if (!mcu->DeleteEventQueue(queueId))
		return xmlerror(env,"Event queue does not exist");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* StartSending(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int partId;
	int media;
	char *sendIp;
	int role;
	int sendPort;
	xmlrpc_value *rtpMap;
	xmlrpc_parse_value(env, param_array, "(iiisiSi)", &confId,&partId,&media,&sendIp,&sendPort,&rtpMap,&role);
	
	
	if (env->fault_occurred)
	{
	    // Try without role argument
	    xmlrpc_env_clean(env);
	    xmlrpc_env_init(env);
	    role = MediaFrame::VIDEO_MAIN;
	    xmlrpc_parse_value(env, param_array, "(iiisiS)", &confId,&partId,&media,&sendIp,&sendPort,&rtpMap);
	
	}
	
	//Get the rtp map
	RTPMap map;

	//Get map size
	int j = xmlrpc_struct_size(env,rtpMap);

	//Parse rtp map
	for (int i=0;i<j;i++)
	{
		xmlrpc_value *key, *val;
		const char *type;
		int codec;
		//Read member
		xmlrpc_struct_read_member(env,rtpMap,i,&key,&val);
		//Read name
		xmlrpc_parse_value(env,key,"s",&type);
		//Read value
		xmlrpc_parse_value(env,val,"i",&codec);
		//Add to map
		map[atoi(type)] = codec;
		//Decrement ref counter
		xmlrpc_DECREF(key);
		xmlrpc_DECREF(val);
	}

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Start sending: bad arguments.");


		//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->StartSending(partId,(MediaFrame::Type)media,sendIp,sendPort,map,(MediaFrame::MediaRole)role);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* SetRTPProperties(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int partId;
	int media;
	int role ;
	
	xmlrpc_value *map;
	
	    xmlrpc_parse_value(env, param_array, "(iiiSi)", &confId,&partId,&media,&map,&role);
	
	if(env->fault_occurred)
	{
	    xmlrpc_env_clean(env);
	    xmlrpc_env_init(env);
	    confId 	= 0;
	    partId 	= 0;
		media 	= 0;
		role = MediaFrame::VIDEO_MAIN;
		xmlrpc_parse_value(env, param_array, "(iiiS)", &confId,&partId,&media,&map);
	
	    if(env->fault_occurred)	
		return xmlerror(env,"bad arguments");
	}
	
	
	//Get the rtp map
	Properties properties;

	//Get map size
	int j = xmlrpc_struct_size(env,map);

	//Parse rtp map
	for (int i=0;i<j;i++)
	{
		xmlrpc_value *key, *val;
		const char *strKey;
		const char *strVal;
		//Read member
		xmlrpc_struct_read_member(env,map,i,&key,&val);
		//Read name
		xmlrpc_parse_value(env,key,"s",&strKey);
		//Read value
		xmlrpc_parse_value(env,val,"s",&strVal);
		//Add to map
		properties[strKey] = strVal;
		//Decrement ref counter
		xmlrpc_DECREF(key);
		xmlrpc_DECREF(val);
	}

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return 0;

		//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->SetRTPProperties(partId,(MediaFrame::Type)media,properties, (MediaFrame::MediaRole) role);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* SetParticipantOrMosaicImage(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId, partId, mosaicId, imageRole;
	char *filename;
	xmlrpc_parse_value(env, param_array, "(iiisi)", &confId,&mosaicId, &partId,&filename, &imageRole);

		//Comprobamos si ha habido error
	if(env->fault_occurred)
	{
	    xmlrpc_env_clean(env);
	    xmlrpc_env_init(env);
	    mosaicId = -1;
	    imageRole = 0; // background
	    xmlrpc_parse_value(env, param_array, "(iis)", &confId,&partId,&filename);
	    if(env->fault_occurred)	
		return xmlerror(env,"bad arguments");
	}
		//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//Get the rtp map
	//La borramos
	int res;
	switch (imageRole)
	{
	     case 0:
		res = conf->SetParticipantBackground(partId,filename);
		break;
		
	     case 1:
	        res = conf->SetParticipantOverlay(mosaicId, partId,filename);
		break;

	     default:
	        res = -1; // Unsupported
	}

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error: no such participant in mediaserver or non existant file");

	//Devolvemos el resultado
	return xmlok(env,xmlrpc_build_value(env,"(i)",res));
}

xmlrpc_value* SetParticipantDisplayName(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int partId;
	int mosaicId;
	char *name;
	int scriptCode;

	xmlrpc_parse_value(env, param_array, "(iiisi)", &confId, &partId, &mosaicId, &name, &scriptCode);

		//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"bad arguments");

		//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//Get the rtp map
	//La borramos
	int res;

	if (strlen(name) > 0 )
	    res = conf->SetParticipantDisplayName(partId, mosaicId, name,scriptCode);
	else
	    res = conf->SetParticipantDisplayName(partId, mosaicId, NULL,scriptCode);
	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error: no such participant in mediaserver or non existant file");

	//Devolvemos el resultado
	return xmlok(env,xmlrpc_build_value(env,"(i)",res));
}


xmlrpc_value* SetLocalCryptoSDES(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	//Parseamos
	int confId;
	int partId;

	int media, role;
	char *suite;
	char *key;
	xmlrpc_parse_value(env, param_array, "(iiissi)", &confId,&partId,&media,&suite,&key, &role);

	//Comprobamos si ha habido error
	if (env->fault_occurred)
	{
	    // Try without role argument
	    xmlrpc_env_clean(env);
	    xmlrpc_env_init(env);
	    role = MediaFrame::VIDEO_MAIN;
	    xmlrpc_parse_value(env, param_array, "(iiiss)", &confId,&partId,&media,&suite,&key);
	}
	
	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return 0;

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");


	//La borramos
	int res = conf->SetLocalCryptoSDES(partId,(MediaFrame::Type)media,suite,key,
					   (MediaFrame::MediaRole) role);





	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)

		return xmlerror(env,"Error");

	//Devolvemos el resultado

	return xmlok(env);
}
xmlrpc_value* SetRemoteCryptoSDES(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	//Parseamos
	int confId;
	int partId;
	int media, role,keyRank;
	char *suite;
	char *key;
	xmlrpc_value *rtpMap;
	xmlrpc_parse_value(env, param_array, "(iiissii)", &confId,&partId,&media,&suite,&key, &role,&keyRank);

	//Comprobamos si ha habido error
	if (env->fault_occurred)
	{
	    // Try without role argument
	    xmlrpc_env_clean(env);
	    xmlrpc_env_init(env);
	    role = MediaFrame::VIDEO_MAIN;
		keyRank=0;
	    xmlrpc_parse_value(env, param_array, "(iiiss)", &confId,&partId,&media,&suite,&key);
	}
	
	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return 0;

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->SetRemoteCryptoSDES(partId,(MediaFrame::Type) media, 
					    suite, key, (MediaFrame::MediaRole) role,keyRank);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* SetRemoteCryptoDTLS(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	//Parseamos
	int confId;
	int partId;
	int media;
	int role;


	char *setup;
	char *hash;
	char *fingerprint;
	xmlrpc_parse_value(env, param_array, "(iiiisss)", &confId,&partId,&media,&role, &setup,&hash,&fingerprint);


	//Comprobamos si ha habido error
	if (env->fault_occurred)
	{
	    Log("SetRemoteCryptoDTLS: trying parse older version of command.\n");
	    // Try without role argument
	    xmlrpc_env_clean(env);
	    xmlrpc_env_init(env);
	    role = MediaFrame::VIDEO_MAIN;
	    xmlrpc_parse_value(env, param_array, "(iiisss)", &confId,&partId,&media, &setup,&hash,&fingerprint);
	}

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return 0;

		//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->SetRemoteCryptoDTLS(partId,(MediaFrame::Type)media,setup,hash,fingerprint);


	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* GetLocalCryptoDTLSFingerprint(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	std::string fingerprint;
	char *hash;
	xmlrpc_parse_value(env, param_array, "(s)", &hash);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return 0;

	//Check hash
	if (strcasecmp(hash,"sha-1")==0)
		//Gen fingerprint
		fingerprint = DTLSConnection::GetCertificateFingerPrint(DTLSConnection::SHA1);
	else if (strcasecmp(hash,"sha-256")==0)
		//Gen fingerprint
		fingerprint = DTLSConnection::GetCertificateFingerPrint(DTLSConnection::SHA256);
	else
		return 0;
	//Create array
	if ( ! fingerprint.empty() )
	{
		xmlrpc_value* arr = xmlrpc_build_value(env,"(s)",fingerprint.c_str());

		//return
		return xmlok(env,arr);
	}
	else
	{
		return xmlerror(env, "Failed to get DTLS fingerprint.");
	}
}
xmlrpc_value* SetLocalSTUNCredentials(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	//Parseamos
	int confId;
	int partId;
	int media, role;
	char *username;
	char *pwd;
	xmlrpc_value *rtpMap;
	xmlrpc_parse_value(env, param_array, "(iiissi)", &confId,&partId,&media,&username,&pwd, &role);

	if (env->fault_occurred)
	{
	    // Try without role argument
	    xmlrpc_env_clean(env);
	    xmlrpc_env_init(env);
	    role = MediaFrame::VIDEO_MAIN;
	    xmlrpc_parse_value(env, param_array, "(iiiss)", &confId,&partId,&media,&username,&pwd );
	}
	
	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return 0;

		//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->SetLocalSTUNCredentials(partId,(MediaFrame::Type)media,username,pwd,
						(MediaFrame::MediaRole) role );

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* SetRemoteSTUNCredentials(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	//Parseamos
	int confId;
	int partId;
	int media, role;
	char *username;
	char *pwd;
	xmlrpc_value *rtpMap;
	xmlrpc_parse_value(env, param_array, "(iiissi)", &confId,&partId,&media,&username,&pwd,&role);

	if (env->fault_occurred)
	{
	    // Try without role argument
	    xmlrpc_env_clean(env);
	    xmlrpc_env_init(env);
	    role = MediaFrame::VIDEO_MAIN;
	    xmlrpc_parse_value(env, param_array, "(iiiss)", &confId,&partId,&media,&username,&pwd );
	}

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return 0;

		//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->SetRemoteSTUNCredentials(partId,(MediaFrame::Type)media,username,pwd,
						 (MediaFrame::MediaRole) role );

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* StopSending(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int partId;
	int media;
	int role;
	
	xmlrpc_parse_value(env, param_array, "(iiii)", &confId,&partId,&media,&role);
	
	
	if (env->fault_occurred)
	{
	    // Try without role argument
	    xmlrpc_env_clean(env);
	    xmlrpc_env_init(env);
	    role = MediaFrame::VIDEO_MAIN;
		xmlrpc_parse_value(env, param_array, "(iii)", &confId,&partId,&media);
	
	}
	
	//Comprobamos si ha habido error
	if(env->fault_occurred)
		xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->StopSending(partId,(MediaFrame::Type)media,(MediaFrame::MediaRole)role);

	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error stoping sending video");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* StartReceiving(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int partId;
	int media;
	int role;
	int proto;
	int recPort = 0;
	xmlrpc_value *rtpMap;
	xmlrpc_parse_value(env, param_array, "(iiiSii)", &confId,&partId,&media,&rtpMap,&role,&proto);

	if (env->fault_occurred)
	{
	    // Try without role argument
	    xmlrpc_env_clean(env);
	    xmlrpc_env_init(env);
	    proto = MediaFrame::TCP;
		xmlrpc_parse_value(env, param_array, "(iiiSi)", &confId,&partId,&media,&rtpMap,&role);
	
	}
	
	//Get the rtp map
	RTPMap map;

	//Get map size
	int j = xmlrpc_struct_size(env,rtpMap);

	//Parse rtp map
	for (int i=0;i<j;i++)
	{
		xmlrpc_value *key, *val;
		const char *type;
		int codec;
		//Read member
		xmlrpc_struct_read_member(env,rtpMap,i,&key,&val);
		//Read name
		xmlrpc_parse_value(env,key,"s",&type);
		//Read value
		xmlrpc_parse_value(env,val,"i",&codec);
		//Add to map
		map[atoi(type)] = codec;
		//Decrement ref counter
		xmlrpc_DECREF(key);
		xmlrpc_DECREF(val);
	}

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
		int recVideoPort = conf->StartReceiving(partId,(MediaFrame::Type)media,map,(MediaFrame::MediaRole)role,confId,(MediaFrame::MediaProtocol)proto);
	
	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!recVideoPort)
		xmlerror(env,"Could not start receiving media.");
	
	Log("StartReceiving recVideoPort=%i\n",recVideoPort);	
						
	//Devolvemos el resultado
	return xmlok(env,xmlrpc_build_value(env,"(i)",recVideoPort));
}

xmlrpc_value* StopReceiving(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int partId;
	int media;
	int role;
	
	xmlrpc_parse_value(env, param_array, "(iiii)", &confId,&partId,&media,&role);
	
	
	if (env->fault_occurred)
	{
	    // Try without role argument
	    xmlrpc_env_clean(env);
	    xmlrpc_env_init(env);
	    role = MediaFrame::VIDEO_MAIN;
	    xmlrpc_parse_value(env, param_array, "(iii)", &confId,&partId,&media);
	}
	
	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");
	
	//La borramos
	int res = conf->StopReceiving(partId,(MediaFrame::Type)media,(MediaFrame::MediaRole)role);
		
	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error stoping receving video");

	//Devolvemos el resultado
	return xmlok(env);
}

xmlrpc_value* GetSupportedCodecs(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	AudioCodec::Type audioCodecs[] = 
	{
		AudioCodec::PCMA, AudioCodec::PCMU, AudioCodec::GSM,
		AudioCodec::SPEEX16,AudioCodec::G722,AudioCodec::AMR,
		AudioCodec::NELLY8, AudioCodec::NELLY11
	};
	
	 //Parseamos
	int media;
	xmlrpc_parse_value(env, param_array, "(i)", &media);
	xmlrpc_value* arr;

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	switch( (MediaFrame::Type) media)
	{
		case MediaFrame::Audio:
			//Create array
			arr = xmlrpc_array_new(env);
			for (int i=0; i<8; i++)
			{
				xmlrpc_value* val = xmlrpc_build_value(env,"(is)",
								       audioCodecs[i],
								       AudioCodec::GetNameFor( audioCodecs[i] ));
				xmlrpc_array_append_item(env,arr,val);
				//Release
				xmlrpc_DECREF(val);
			}
			break;
			
		// Impleent video and text
		default:
			return xmlerror(env,"Fault occurred: media not supported");
	}
	return xmlok(env,arr);
}

xmlrpc_value* IsCodecSupported(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	 //Parseamos
	int media, codec;
	xmlrpc_parse_value(env, param_array, "(ii)", &media, &codec);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	return xmlok(env,xmlrpc_build_value(env,"(s)",GetNameForCodec( (MediaFrame::Type) media, (DWORD) codec) ));
}

xmlrpc_value* AcceptDocSharingRequest(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int partId;
	xmlrpc_parse_value(env, param_array, "(ii)", &confId, &partId);
	Log(">AcceptDocSharingRequest  [partId:%d]\n",partId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");
	
	//La borramos
	int res = conf->AcceptDocSharingRequest(confId,partId);
		
	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error accept doc sharing request");

	//Devolvemos el resultado
	return xmlok(env);

	
}

xmlrpc_value* RefuseDocSharingRequest(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int partId;
	xmlrpc_parse_value(env, param_array, "(ii)", &confId, &partId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->RefuseDocSharingRequest(confId,partId);
		
	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error refuse doc sharing request");

	//Devolvemos el resultado
	return xmlok(env);

	
}

xmlrpc_value* StopDocSharing(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int partId=0;
	xmlrpc_parse_value(env, param_array, "(ii)", &confId, &partId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->StopDocSharing(confId,partId);
		
	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error accept doc sharing request");

	//Devolvemos el resultado
	return xmlok(env);

	
}

xmlrpc_value* SetDocSharingMosaic(xmlrpc_env *env, xmlrpc_value *param_array, void *user_data)
{
	MCU *mcu = (MCU *)user_data;
	MultiConf *conf = NULL;

	 //Parseamos
	int confId;
	int mosaicId=0;
	xmlrpc_parse_value(env, param_array, "(ii)", &confId, &mosaicId);

	//Comprobamos si ha habido error
	if(env->fault_occurred)
		return xmlerror(env,"Fault occurred");

	//Obtenemos la referencia
	if(!mcu->GetConferenceRef(confId,&conf))
		return xmlerror(env,"Conference does not exist");

	//La borramos
	int res = conf->SetDocSharingMosaic(mosaicId);
		
	//Liberamos la referencia
	mcu->ReleaseConferenceRef(confId);

	//Salimos
	if(!res)
		return xmlerror(env,"Error set doc sharing mosaic");

	//Devolvemos el resultado
	return xmlok(env);

	
}

XmlHandlerCmd mcuCmdList[] =
{
	{"EventQueueCreate",MCUEventQueueCreate},
	{"EventQueueDelete",MCUEventQueueDelete},
	{"CreateConference",CreateConference},
	{"UpdateConference",UpdateConference},
	{"DeleteConference",DeleteConference},
	{"GetConferences",GetConferences},
	{"CreateMosaic",CreateMosaic},
	{"SetMosaicOverlayImage",SetMosaicOverlayImage},
	{"ResetMosaicOverlay",ResetMosaicOverlay},
	{"DeleteMosaic",DeleteMosaic},
	{"CreateSidebar",CreateSidebar},
	{"DeleteSidebar",DeleteSidebar},
	{"CreateParticipant",CreateParticipant},
	{"DeleteParticipant",DeleteParticipant},
	{"StartBroadcaster",StartBroadcaster},
	{"StopBroadcaster",StopBroadcaster},
	{"StartPublishing",StartPublishing},
	{"StopPublishing",StopPublishing},
	{"StartRecordingBroadcaster",StartRecordingBroadcaster},
	{"StopRecordingBroadcaster",StopRecordingBroadcaster},
	{"StartRecordingParticipant",StartRecordingParticipant},
	{"StopRecordingParticipant",StopRecordingParticipant},
	{"SetVideoCodec",SetVideoCodec},
	{"StartSending",StartSending},
	{"StopSending",StopSending},
	{"StartReceiving",StartReceiving},
	{"StopReceiving",StopReceiving},
	{"SetAudioCodec",SetAudioCodec},
	{"SetTextCodec",SetTextCodec},	
	{"SetAppCodec",SetAppCodec},
	{"SetCompositionType",SetCompositionType},
	{"SetMosaicSlot",SetMosaicSlot},
	{"AddConferenceToken",AddConferenceToken},
	{"AddMosaicParticipant",AddMosaicParticipant},
	{"RemoveMosaicParticipant",RemoveMosaicParticipant},
	{"AddSidebarParticipant",AddSidebarParticipant},
	{"RemoveSidebarParticipant",RemoveSidebarParticipant},
	{"CreatePlayer",CreatePlayer},
	{"DeletePlayer",DeletePlayer},
	{"StartPlaying",StartPlaying},
	{"StopPlaying",StopPlaying},
	{"SendFPU",SendFPU},
	{"SetMute",SetMute},
	{"GetParticipantStatistics",GetParticipantStatistics},
	{"AddParticipantInputToken",AddParticipantInputToken},
	{"AddParticipantOutputToken",AddParticipantOutputToken},
	{"SetParticipantMosaic",SetParticipantMosaic},
	{"SetParticipantSidebar",SetParticipantSidebar},
	{"SetParticipantBackground",SetParticipantOrMosaicImage},
	{"SetParticipantOrMosaicImage",SetParticipantOrMosaicImage},
	{"SetParticipantDisplayName",SetParticipantDisplayName},
	{"SetRemoteCryptoSDES",SetRemoteCryptoSDES},
	{"SetLocalCryptoSDES",SetLocalCryptoSDES},
	{"GetLocalCryptoDTLSFingerprint",GetLocalCryptoDTLSFingerprint},
	{"SetRemoteCryptoDTLS",SetRemoteCryptoDTLS},
	{"SetLocalSTUNCredentials",SetLocalSTUNCredentials},
	{"SetRemoteSTUNCredentials",SetRemoteSTUNCredentials},
	{"SetRTPProperties",SetRTPProperties},
	{"GetMosaicPositions",GetMosaicPositions},
	{"GetSupportedCodecs",GetSupportedCodecs},
	{"IsCodecSupported",IsCodecSupported},
	{"AcceptDocSharingRequest",AcceptDocSharingRequest},
	{"RefuseDocSharingRequest",RefuseDocSharingRequest},
	{"StopDocSharing",StopDocSharing},
	{"SetDocSharingMosaic",SetDocSharingMosaic},
	{NULL,NULL}
};
