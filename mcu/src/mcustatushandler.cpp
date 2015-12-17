/* 
 * File:   mcustatushandler.cpp
 * Author: ebuu
 * 
 * Created on 10 novembre 2014, 11:39
 */

#include "mcustatushandler.h"

McuStatusHandler::McuStatusHandler(MCU * mcu)
{
    this->mcu = mcu;
}

int McuStatusHandler::ProcessRequest(TRequestInfo *req,TSession * const ses)
{
    	Log(">ProcessRequest [uri:%s]\n",req->uri);

        if (strncmp(req->uri, "/status/mcu/", 12) == 0)
        {
            int confid = 0, partid = 0;
            int len;
            char strid[20];

            const char *pt = req->uri +12;
            const char *pt2 = strchr(pt + 1, '/');

            if (pt2 == NULL)
            {
                // Affichage d'une conference
                confid = atoi(pt);
                PrintConfStatus(ses, confid, false);
            }
            else
            {
                len = (pt2-pt);
                strncpy(strid, pt, len);
                strid[len] = '\0';
                confid = atoi(strid);

                // Now parse participant ID
                pt = pt2+1;
                pt2 = strchr(pt, '/');

                if (pt2 != NULL)
                {
                    len = pt2 - pt;
                }
                else
                {
                    len = strlen(pt);
                }

                strncpy(strid, pt, len);
                strid[len] = '\0';

                partid = atoi(strid);

                PrintPartStatus(ses, confid, partid);
            }
        }
        else if (strcmp(req->uri, "/status/mcu") == 0)
        {
            ListConferences(ses);
	    return 1;
        }
        XmlRpcServer::SendError(ses, 404, "No such command");
	return 1;
}

void McuStatusHandler::ListConferences(TSession * const ses)
{
    MCU::ConferencesInfo lst;

    if (mcu->GetConferenceList(lst))
    {
        if (lst.empty())
        {
             XmlRpcServer::SendError(ses, 404, "No active conference");
             return;
        }

        std::string response;

        for (MCU::ConferencesInfo::iterator it = lst.begin();
             it != lst.end(); it++)
        {
            char str[200];

            sprintf(str, "%d|%ls|%d\n",
                    it->second.id,
                    it->second.name.c_str(),
                    it->second.numPart);
            response += str;
        }

        XmlRpcServer::SendResponse(ses,200,
            response.c_str(), response.length() );
    }
}

void McuStatusHandler::PrintConfStatus(TSession * const ses, int confid, bool listPart)
{
    MultiConf *conf = NULL;
    Debug("Printing status of conf %d.\n", confid);

    if(!mcu->GetConferenceRef(confid,&conf))
    {
        XmlRpcServer::SendError(ses, 404, "No such conference");
        return;
    }

    std::string response;
    int code = conf->DumpInfo(response);

    if ( code == 200 )
	conf->DumpMixerInfo(0, MediaFrame::Audio, response);

    if ( code == 200 )
	conf->DumpMixerInfo(1, MediaFrame::Audio, response);

    mcu->ReleaseConferenceRef(confid);
    if (code == 200)
    	XmlRpcServer::SendResponse(ses, 200, response.c_str(), response.length() );
    else
        XmlRpcServer::SendError(ses, code, response.c_str());

}

void McuStatusHandler::PrintPartStatus(TSession * const ses, int confid, int partid)
{
    MultiConf *conf = NULL;
    int code;

    if (!mcu->GetConferenceRef(confid,&conf))
    {
        XmlRpcServer::SendError(ses, 404, "No such conference");
        return;
    }

    std::string response;
    code = conf->DumpParticipantInfo(partid, response);
    mcu->ReleaseConferenceRef(confid);

    if ( code == 200 )
    {
        XmlRpcServer::SendResponse(ses, 200, response.c_str(), response.length() );
    }
    else
    {
        XmlRpcServer::SendError(ses, code, response.c_str());
    }

}

McuStatusHandler::~McuStatusHandler()
{
}

