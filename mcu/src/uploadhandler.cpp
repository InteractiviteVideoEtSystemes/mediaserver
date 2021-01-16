/* 
 * File:   uploadhandler.cpp
 * Author: Sergio
 * 
 * Created on 10 de enero de 2013, 11:33
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <log.h>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "uploadhandler.h"
#include "http.h"
#include "stringparser.h"

UploadHandler::UploadHandler(Listener *listener)
{
	//Store listener
	this->listener = listener;
}

/**************************************
* ProcessRequest
*	Procesa una peticion
*************************************/
int UploadHandler::ProcessRequest(TRequestInfo *req,TSession * const ses)
{
	char filename[1024];
	int fd = -1;
	int code = 404;
	char* buffer = NULL;
	DWORD size = 0;
	std::string boundary;
	std::string line;
	const char* type = NULL;
	ContentType* contentType = NULL;

	Log("-ProcessRequest [%s]\n",req->uri);

	//Get content length header
	const char * content_length = RequestHeaderValue(ses, "content-length");

	//Check content length
	if (!content_length)
		//Error
		return XmlRpcServer::SendError(ses,500,"Content-Length header not present");

	//Obtenemos el entero
	size = atoi(content_length);

	//Check size
	if (!size)
		//Error
		return XmlRpcServer::SendError(ses,500,"Content-Length header value invalid");

	//Creamos un buffer para el body
	buffer = (char*) malloc(size);

	//Create string parser
	StringParser parser(buffer,size);

	//Get content type
	type = RequestHeaderValue(ses, "content-type");

	//Check type
	if (!type)
		//Error
		goto error;

	//Parse content type
	contentType = ContentType::Parse(type,strlen(type));

	//Check parsing
	if (!contentType)
		//Error
		goto error;

	//Ensure it is a multipart/form-data
	if (contentType->GetType().compare("multipart")!=0 || contentType->GetSubType().compare("form-data"))
		//goto error
		goto error;

	//Check it has boundary param
	if (!contentType->HasParameter("boundary"))
		//goto error
		goto error;

	//Get boundary and prepend the t
	boundary = "--" + contentType->GetParameter("boundary");

	//Get body
	if (!XmlRpcServer::GetBody(ses,buffer,size))
		//error
		goto error;

	//First line shall start with a boundary
	if (!parser.CheckString(boundary))
		//Error
		goto error;
	
	//Get first line
	if (!parser.ParseLine())
		//error
		goto error;

	//Get line
	line = parser.GetValue();

	//For all body contents
	while(!parser.IsEnded())
	{
		//Body part headers
		Headers headers;

		//Not in file
		fd = -1;

		//Check next line
		if (!parser.ParseLine())
			//error
			goto error;
		//Get header line
		line = parser.GetValue();
		
		//Read headers
		while(!line.empty())
		{
			//Parse header line
			if (!headers.ParseHeader(line))
				//error
				goto error;
			//Check next line
			if (!parser.ParseLine())
				//error
				goto error;
			//Get next header line
			line = parser.GetValue();
		}

		//Check content type
		if (headers.HasHeader("Content-Disposition") && headers.HasHeader("Content-Type"))
		{
			//Parse body content type
			ContentType* bodyType = ContentType::Parse(headers.GetHeader("Content-Type"));
			//Parse body content disposition
			ContentDisposition* bodyDisposition = ContentDisposition::Parse(headers.GetHeader("Content-Disposition"));
			//If we have it and it is image
			if (bodyType && bodyDisposition && bodyDisposition->HasParameter("filename"))
			{
				//Create filename
				snprintf(filename,1024,"/tmp/%s-XXXXXX", 
                                         basename(bodyDisposition->GetParameter("filename").c_str()));
				// Open
				fd = mkostemp(filename, O_CREAT|O_WRONLY|O_TRUNC);
				//fd = open(filename,O_CREAT|O_WRONLY|O_TRUNC, 0664);
				//Log
				if (fd > 0) 
				{
				    Log("-Got content %s/%s saving to %s\n",
					bodyType->GetType().c_str(),
					bodyType->GetSubType().c_str(),filename);
				}
				else
				{
				    Error("Got content but failed to create a temp file to save it. errno=%d.", errno);
				    goto error;
				}
			}
		}
		
		//Read content
		while(!parser.CheckString(boundary))
		{
			//Get init of line
			char* s = parser.Mark();
			//Get next line
			if (!parser.ParseLine())
				//error
				goto error;
			//Check if content is a file
			if (fd>0)
				//Write
				write(fd,s,parser.Mark()-s);
			
		}
		//close file
		if (fd>0)
		{
			//Close
			close(fd);
			chmod(filename, 0664);

			//Check listener
			if (listener)
			{
				//Call it
				code = listener->onFileUploaded(req->uri,filename);
				//Delete file
				unlink(filename);
			}
		}
		//Get next line
		if (!parser.ParseLine())
			//error
			goto error;
		//Get last line (boundary)
		line = parser.GetValue();
	}

	//Send OK
	XmlRpcServer::SendResponse(ses,code,NULL,0);

	//Delete content type
	delete (contentType);
	
	//Free buffer
	free(buffer);

	//OK
	return 1;
error:
	//check
	if (contentType) delete(contentType);

	//LIberamos el buffer
	if (buffer) free(buffer);

	if (fd > 0)
	{
		close(fd);
		if (filename[0]) unlink(filename);
	}
	//Send Error
	return XmlRpcServer::SendError(ses,500,"Error processing request");
}

