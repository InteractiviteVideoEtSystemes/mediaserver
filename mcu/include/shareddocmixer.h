/* 
 * File:   appmixer.h
 * Author: Sergio
 *
 * Created on 15 de enero de 2013, 15:38
 */


#include <string>

#include "config.h"
#include "media.h"
#include "video.h"
#include "logo.h"
#include "participant.h"
#include "bfcp_server.h"

#ifndef SHAREDDOCMIXER_H
#define	SHAREDDOCMIXER_H

class MultiConf;

class SharedDocMixer  : 
 public VideoOutput,
 public BFCP_Server::ServerEvent 
{
public:
	SharedDocMixer();
	~SharedDocMixer();
	int Init(VideoOutput *output, Logo *logo,MultiConf *conf);
	int ShareSecondaryStream(Participant *part);
	
	int initDocSharing( Participant *part,char *sendIp,int sendPort);
	int AcceptDocSharingRequest(int confId, Participant *part);
	int RefuseDocSharingRequest(int confId, Participant *part);
	
	int StopSharing();
	int StopSharing(Participant *part);
	virtual int NextFrame(BYTE *pic);
	virtual int SetVideoSize(int width,int height) ;
	
	int addParticipant(int confId, Participant *part,Participant::DocSharingMode docSharingMode, MediaFrame::MediaProtocol proto = MediaFrame::TCP);
	int removeParticipant(Participant *part);
	int getServerPort(Participant *part);
	int getAvailablePort();
	bool StopBfcpServer();
	int		GetSharedMosaic(){return sharedMosaic;}
	void	SetSharedMosaic(int mosaicId);
	
	int End();

private:
	
	Logo*			logo;
	Participant*	part;
	VideoOutput*	output;
	MultiConf* 		conf;
	int				confId;
	int				sharedMosaic;
	
	
	struct BFCPInfo
	{
		int	transactionId;
		int floorRequestID;
		e_bfcp_status bfcp_status;
	};
	
	//BFCP
	BFCP_Server* 	bfcp_server;
	typedef std::map<int,struct BFCPInfo> Transactions;
	
	Transactions transactions;
	pthread_mutex_t	mutex;
	
	
	
	bool StartBfcpServer(int confId,Participant *part);
	
	bool 	GetBfcpInfo(int partId,struct BFCPInfo &info);

	
	// BFCP_Server::ServerEvent
    virtual bool OnBfcpServerEvent(BFCP_fsm::e_BFCP_ACT p_evt , BFCP_fsm::st_BFCP_fsm_event* p_FsmEvent );
   
	
 
};

#endif	/* SHAREDDOCMIXER_H */

