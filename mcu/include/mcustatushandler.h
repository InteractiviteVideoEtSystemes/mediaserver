/* 
 * File:   mcustatushandler.h
 * Author: ebuu
 *
 * Created on 10 novembre 2014, 11:39
 */

#ifndef MCUSTATUSHANDLER_H
#define	MCUSTATUSHANDLER_H

#include "xmlrpcserver.h"
#include "mcu.h"

class McuStatusHandler : public Handler
{
public:
    McuStatusHandler(MCU * mcu);
    virtual int ProcessRequest(TRequestInfo *req,TSession * const ses);
    
    virtual ~McuStatusHandler();

private:
    void ListConferences(TSession * const ses);
    void PrintConfStatus(TSession * const ses, int confid, bool listPart);
    void PrintPartStatus(TSession * const ses, int confid, int partid);
    
private:
    MCU * mcu;
};

#endif	/* MCUSTATUSHANDLER_H */

