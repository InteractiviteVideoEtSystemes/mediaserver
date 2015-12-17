#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include "log.h"
#include "multiconf.h"
#include "rtpparticipant.h"
#include "rtmpparticipant.h"

/************************
* MultiConf
* 	Constructor
*************************/
MultiConf::MultiConf(const std::wstring &tag) : broadcast(tag), sharedDocMixer(), videoMixer(tag)
{
	//Guardamos el nombre
	this->tag = tag;

	//No watcher
	watcherId = 0;

	//Init counter
	maxPublisherId = 1;

	//Inicializamos el contador
	maxId = 500;

	//Y no tamos iniciados
	inited = 0;
	broadcastId = 0;
        //No recorder
        recorder = NULL;
}

/************************
* ~ MultiConf
* 	Destructor
*************************/
MultiConf::~MultiConf()
{
	//Pa porsi
	if (inited)
		End();
        //Delete recoreder
        if (recorder)
                //Delete
                delete(recorder);
}

void MultiConf::SetListener(Listener *listener,void* param)
{
	//Store values
	this->listener = listener;
	this->param = param;
}

/************************
* Init
* 	Constructo
*************************/
int MultiConf::Init(int vad, DWORD rate)
{
	Log("-Init multiconf [vad:%d,rate:%d]\n",vad,rate);

	//We are inited
	inited = true;
	//Init audio mixers
	int res = audioMixer.Init(vad,rate);
	//Set vad mode
	videoMixer.SetVADMode((VideoMixer::VADMode)vad);
	//Check if we need to use vad
	if (vad)
		//Set VAD proxyG
		videoMixer.SetVADProxy(&audioMixer);
	//Init video mixer with dedault parameters
	res &= videoMixer.Init(Mosaic::mosaic2x2,CIF);
	//Init text mixer
	res &= textMixer.Init();

	//Check if we are inited
	if (!res)
		//End us
		End();

	//Get the id
	watcherId = maxId++;

	//Create audio and text watcher
	audioMixer.CreateMixer(watcherId);
	std::wstring name = std::wstring(L"watcher");
	textMixer.CreateMixer(watcherId,name);

	//Init the audio encoder
	audioEncoder.Init(audioMixer.GetInput(watcherId));
	//Init the text encoder
	textEncoder.Init(textMixer.GetInput(watcherId));

	//Set codec
	audioEncoder.SetAudioCodec(AudioCodec::PCMA);

	//Start mixers
	audioMixer.InitMixer(watcherId,0);
	textMixer.InitMixer(watcherId);

	//Start encoding
	audioEncoder.StartEncoding();
	textEncoder.StartEncoding();

	//Create one mixer for the app Mixer ouuput
	videoMixer.CreateMixer(AppMixerId);
	
	//Create one mixer for the shared video output
	videoMixer.CreateMixer(SharedDocMixerId);

	//Init
	appMixer.Init(videoMixer.GetOutput(AppMixerId));
	
	//Init
	sharedDocMixer.Init(videoMixer.GetOutput(SharedDocMixerId),videoMixer.GetLogo(),this);

	//Init mixer for the app mixer
	videoMixer.InitMixer(AppMixerId,-1);
	
	//Init mixer for the app mixer
	videoMixer.InitMixer(SharedDocMixerId,-1);

	return res;
}

/************************
* SetCompositionType
* 	Set mosaic type and size
*************************/
int MultiConf::SetCompositionType(int mosaicId,Mosaic::Type comp,int size)
{
	Log("-SetCompositionType [mosaic:%d,comp:%d,size:%d]\n",mosaicId,comp,size);

	//POnemos el tipo de video mixer
	return videoMixer.SetCompositionType(mosaicId,comp,size);
}

/*****************************
* StartBroadcaster
* 	Create FLV Watcher port
*******************************/
int MultiConf::StartBroadcaster(int mosaicId, int sidebarId)
{
    std::wstring name(L"broadcaster");

    Log(">StartBroadcaster [m=%d, s=%d]\n", mosaicId, sidebarId);

    //Cehck if running
    if (!inited)
		//Exit
		return Error("-Cannot start broadcaster: conference not inited.\n");
	
    if (broadcastId == 0)
    {
        // Broadcaster was not running - Get the id
	broadcastId = maxId++;
	Log("-StartBroadcaster: creating broadcaster with ID %d.\n", broadcastId);
	//Create mixers
	videoMixer.CreateMixer(broadcastId);
	audioMixer.CreateMixer(broadcastId);
	textMixer.CreateMixer(broadcastId,name);

	//Create the broadcast session without limits
	broadcast.Init(0,0);

	//Init it flv encoder
	flvEncoder.Init(audioMixer.GetInput(broadcastId),
			videoMixer.GetInput(broadcastId),
			textMixer.GetInput(broadcastId));

	//Init mixers
	videoMixer.InitMixer(broadcastId,mosaicId);
	audioMixer.InitMixer(broadcastId,sidebarId);
	textMixer.InitMixer(broadcastId);

	//Start encoding
	flvEncoder.StartEncoding();
    }
    else
    {
	audioMixer.SetMixerSidebar(broadcastId,sidebarId);
	videoMixer.SetMixerMosaic(broadcastId,mosaicId);
	Log("-StartBroadcaster: broadcaster was already running with ID %d. "
	    "Changing mosaicID = %d /sidebarID = %d to.\n", broadcastId, mosaicId, sidebarId );
    }
    Log("<StartBroadcaster\n");

    return 1;
}

int MultiConf::StartRecordingBroadcaster(const char* filename,int mosaicId, int sidebarId)
{
    std::wstring name(L"recorder");
    //int mosaicId = 0, sidebarId = 0;

	//Log filename
	Log("-Recording conference into [file:\"%s\"]\n",filename);
	
	//Find last "."
	const char* ext = (const char*)strrchr(filename,'.');

	//If not found
	if (!ext)
		//Error
		return Error("Extension not found for [file:\"%s\"]\n",filename);

	if (recorder != NULL)
	{
		return Error("Recording is already active.\n");
	}
	
	//Check file name
	if (strncasecmp(ext,".flv",4)==0)
		//FLV
		recorder = new FLVRecorder();
	else if (strncasecmp(ext,".mp4",4)==0)
		//MP4
		recorder = new MP4Recorder();
	else
		//Error
		return Error("Unsupported file type extension [ext:\"%s\"]\n",ext);
		
	videoMixer.CreateMixer(RecorderId);
	audioMixer.CreateMixer(RecorderId);
	textMixer.CreateMixer(RecorderId,name);

	//Init it flv encoder
	recEncoder.Init(audioMixer.GetInput(RecorderId),
			videoMixer.GetInput(RecorderId),
			textMixer.GetInput(RecorderId));

	//Init mixers
	videoMixer.InitMixer(RecorderId,mosaicId);
	audioMixer.InitMixer(RecorderId,sidebarId);
	textMixer.InitMixer(RecorderId);

	//Open file for recording
	if (!recorder->Create(filename))
		//Fail
		goto start_recording_failed;

	//And start recording
	if (!recorder->Record())
		//Exit
		return 0;

	//Check type
	switch (recorder->GetType())
	{
		case RecorderControl::FLV:
			//Set RTMP listener
			recEncoder.AddMediaListener((FLVRecorder*)recorder);
			break;
		case RecorderControl::MP4:
			//Set change codec and add a media listener
			recEncoder.SetCodec(AudioCodec::PCMA);
			recEncoder.AddMediaFrameListener((MP4Recorder*)recorder);
			break;
	}
	
	//Start encoding
	recEncoder.StartEncoding();

	//OK
	return 1;

start_recording_failed:
	videoMixer.EndMixer(RecorderId);
	audioMixer.EndMixer(RecorderId);
	textMixer.EndMixer(RecorderId);
	videoMixer.DeleteMixer(RecorderId);
	audioMixer.DeleteMixer(RecorderId);
	textMixer.DeleteMixer(RecorderId);
	delete recorder;
	recorder = NULL;
	return 0;
}

int MultiConf::StopRecordingBroadcaster()
{
	if (recorder == NULL)
	{
		return Error("-recorder: recorder is already stopped.\n");
	}
	
	Log(">StopRecordingBroadcaster\n");
	//Stop endoding
	recEncoder.StopEncoding();
	recEncoder.End();
	
	//Close recorder
	recorder->Stop();
	recorder->Close();
		
		//Check type
	switch (recorder->GetType())
	{
		case RecorderControl::FLV:
			//Set RTMP listener
			recEncoder.RemoveMediaListener((FLVRecorder*)recorder);
			break;
		case RecorderControl::MP4:
			//Set RTMP listener
			recEncoder.RemoveMediaFrameListener((MP4Recorder*)recorder);
			break;
	}

	videoMixer.EndMixer(RecorderId);
	audioMixer.EndMixer(RecorderId);
	textMixer.EndMixer(RecorderId);



	delete recorder;
	recorder = NULL;
	videoMixer.DeleteMixer(RecorderId);
	audioMixer.DeleteMixer(RecorderId);
	textMixer.DeleteMixer(RecorderId);

	Log("<StopRecordingBroadcaster\n");
	//Exit
	return 1;
}

/*****************************
* StopBroadcaster
*       End FLV Watcher port
******************************/
int MultiConf::StopBroadcaster()
{
	//If no watcher
	if (!broadcastId)
		//exit
		return 0;

	Log(">StopBroadcaster\n");

	//Check recorder
	if (recorder)
		//Close it
		recorder->Close();

	Log("-flvEncoder.StopEncoding\n");
	//Stop endoding
	flvEncoder.StopEncoding();

	Log("-Ending mixers\n");
	//End mixers
	videoMixer.EndMixer(broadcastId);
	audioMixer.EndMixer(broadcastId);
	textMixer.EndMixer(broadcastId);

	Log("flvEncoder.End\n");
	//End Transmiter
	flvEncoder.End();
	recEncoder.End();

	Log("Ending publishers");
	//For each publisher
	Publishers::iterator it = publishers.begin();

	//until last
	while (it!=publishers.end())
	{
		//Get first publisher info
		PublisherInfo info = it->second;
		//Check stream
		if (info.stream)
		{
			//Un publish
			info.stream->UnPublish();
			//And close
			info.stream->Close();
			//Remove listener
			flvEncoder.RemoveMediaListener((RTMPMediaStream::Listener*)info.stream);
			//Delete it
			delete(info.stream);
		}
		//Disconnect
		info.conn->Disconnect();
		//Delete connection
		delete(info.conn);
		//Remove
		publishers.erase(it++);
	}

	//Stop broacast
	broadcast.End();

	//End mixers
	videoMixer.DeleteMixer(broadcastId);
	audioMixer.DeleteMixer(broadcastId);
	textMixer.DeleteMixer(broadcastId);

	//Unset watcher id
	broadcastId = 0;

	Log("<StopBroadcaster\n");

	return 1;
}

/************************
* SetMosaicSlot
* 	Set slot position on mosaic
*************************/
int MultiConf::SetMosaicSlot(int mosaicId,int slot,int id)
{
	Log("-SetMosaicSlot [mosaic:%d,slot:%d,id:%d]\n",mosaicId,slot,id);

	//Set it
	return videoMixer.SetSlot(mosaicId,slot,id);
}


int MultiConf::GetMosaicPositions(int mosaicId,std::list<int> &positions)
{
	//Set it
	return videoMixer.GetMosaicPositions(mosaicId,positions);
}
/************************
* AddMosaicParticipant
* 	Show participant in a mosaic
*************************/
int MultiConf::AddMosaicParticipant(int mosaicId,int partId)
{
	//Set it
	return videoMixer.AddMosaicParticipant(mosaicId,partId);
}

/************************
* RemoveMosaicParticipant
* 	Unshow a participant in a mosaic
*************************/
int MultiConf::RemoveMosaicParticipant(int mosaicId,int partId)
{
	Log("-RemoveMosaicParticipant [mosaic:%d,partId:]\n",mosaicId,partId);
	
	switch ( partId )
    {
		case AppMixerId:
				appMixer.DisplayImage( videoMixer.GetLogo() );
				
				break;
		case SharedDocMixerId:
				
				break;

		default:
				break;
    }

	//Set it
	return videoMixer.RemoveMosaicParticipant(mosaicId,partId);
}


/************************
* AddSidebarParticipant
* 	Show participant in a Sidebar
*************************/
int MultiConf::AddSidebarParticipant(int sidebarId,int partId)
{
	//Set it
	return audioMixer.AddSidebarParticipant(sidebarId,partId);
}

/************************
* RemoveSidebarParticipant
* 	Unshow a participant in a Sidebar
*************************/
int MultiConf::RemoveSidebarParticipant(int sidebarId,int partId)
{
	Log("-RemoveSidebarParticipant [sidebar:%d,partId:]\n",sidebarId,partId);

	//Set it
	return audioMixer.RemoveSidebarParticipant(sidebarId,partId);
}

/************************
* CreateMosaic
* 	Add a mosaic to the conference
*************************/
int MultiConf::CreateMosaic(Mosaic::Type comp,int size)
{
	return videoMixer.CreateMosaic(comp,size);
}

/************************
* CreateSidebar
* 	Add a sidebar to the conference
*************************/
int MultiConf::CreateSidebar()
{
	return audioMixer.CreateSidebar();
}

/************************
* SetMosaicOverlayImage
* 	Set mosaic overlay image
*************************/
int MultiConf::SetMosaicOverlayImage(int mosaicId,const char* filename)
{
	return videoMixer.SetOverlayImage(mosaicId,0, filename);
}
/************************
* ResetMosaicOverlayImage
* 	Reset mosaic overlay image
*************************/
int MultiConf::ResetMosaicOverlay(int mosaicId)
{
	return videoMixer.ResetOverlayImage(mosaicId, 0);
}
/************************
* DeleteMosaic
* 	delete mosaic
*************************/
int MultiConf::DeleteMosaic(int mosaicId)
{
	return videoMixer.DeleteMosaic(mosaicId);
}

/************************
* DeleteSidebar
* 	delete sidebar
*************************/
int MultiConf::DeleteSidebar(int sidebarId)
{
	return audioMixer.DeleteSidebar(sidebarId);
}

/************************
* CreateParticipant
* 	A�ade un participante
*************************/
int MultiConf::CreateParticipant(int mosaicId,int sidebarId,std::wstring name,Participant::Type type)
{
	wchar_t uuid[64];
	Participant *part = NULL;

	Log(">CreateParticipant [mosaic:%d]\n",mosaicId);

	//SI no tamos iniciados pasamos
	if (!inited)
		return Error("Not inited\n");

	//Get lock
	participantsLock.WaitUnusedAndLock();

	//Obtenemos el id
	int partId = maxId++;

	//Create uuid
	swprintf(uuid,64,L"%ls@%d",tag.c_str(),partId);

	//Unlock
	participantsLock.Unlock();

	//Le creamos un mixer
	if (!videoMixer.CreateMixer(partId))
		return Error("Couldn't set video mixer\n");

	//Y el de audio
	if (!audioMixer.CreateMixer(partId))
	{
		//Borramos el de video
		videoMixer.DeleteMixer(partId);
		//Y salimos
		return Error("Couldn't set audio mixer\n");
	}

	//And text
	if (!textMixer.CreateMixer(partId,name))
	{
		//Borramos el de video y audio
		videoMixer.DeleteMixer(partId);
		audioMixer.DeleteMixer(partId);
		//Y salimos
		return Error("Couldn't set text mixer\n");
	}

	//Depending on the type
	switch(type)
	{
		case Participant::RTP:
			//Create RTP Participant
			part = new RTPParticipant(partId,std::wstring(uuid));
			break;
		case Participant::RTMP:
			part = new RTMPParticipant(partId);
			//Create RTP Participant
			break;

		default:
			videoMixer.DeleteMixer(partId);
			audioMixer.DeleteMixer(partId);
			textMixer.DeleteMixer(partId);
			return Error("-CreateParticipant: this participant type %d is not supported.\n", type);
	}

	//Set inputs and outputs
	part->SetVideoInput(videoMixer.GetInput(partId));
	part->SetVideoOutput(videoMixer.GetOutput(partId));
	
	part->SetAudioInput(audioMixer.GetInput(partId));
	part->SetAudioOutput(audioMixer.GetOutput(partId));
	part->SetTextInput(textMixer.GetInput(partId));
	part->SetTextOutput(textMixer.GetOutput(partId));
	
	//Init participant
	if (part->Init() > 0)
	{
		//E iniciamos el mixer
		videoMixer.InitMixer(partId,mosaicId);
		audioMixer.InitMixer(partId,sidebarId);
		textMixer.InitMixer(partId);
				
		//Get lock
		participantsLock.WaitUnusedAndLock();

		//Lo insertamos en el map
		participants[partId] = part;

		//Unlock
		participantsLock.Unlock();

		//Set us as listener
		part->SetListener(this);
		Log("-CreateParticipant: part %d is now ready.\n");
	}
	else
	{
		Error("-Create participant: failed to init participant %d. Destroying.\n",  partId);
		//Destroy participant
		DestroyParticipant(partId,part);
		return 0;
	}

	Log("<CreateParticipant [%d]\n",partId);

	return partId;
}

/************************
* DeleteParticipant
* 	destroy a participant asynchronously
*************************/
struct PartDestructionJob
{
    MultiConf * c;
    Participant *p;
};

int MultiConf::DeleteParticipant(int id)
{
	Log(">DeleteParticipant [%d]\n",id);

	//Stop recording participant just in case
	StopRecordingParticipant(id);

	//Block
	if ( participantsLock.WaitUnusedAndLock(2000) != 1)
        {
            return Error("DeleteParticipant: failed to access participant list\n");
        }
	
	//El iterator
	Participants::iterator it = participants.find(id);

	//Si no esta
	if (it == participants.end())
	{
		//Unlock
		participantsLock.Unlock();
		//Exit
		return Error("Participant not found\n");
	}

	//LO obtenemos
	Participant *part = it->second;

	//Y lo quitamos del mapa
	participants.erase(it);

	//Unlock
	participantsLock.Unlock();

	//Destroy participatn
	int ret = DestroyParticipant(id,part);

	Log("<DeleteParticipant [%d] --> %s\n",id, ret ? "ok" : "nok");

	return ret;
}


int MultiConf::DestroyParticipant(int partId,Participant* part)
{
	Log(">DestroyParticipant [%d]\n",partId);
	bool confEmpty;
	
	sharedDocMixer.StopSharing(part);
	sharedDocMixer.removeParticipant(part);

	//End participant audio and video streams
	int ret = part->End();

	Log("-DestroyParticipant ending mixers [%d]\n",partId);

	//End participant audio and text mixers
	audioMixer.EndMixer(partId);
	textMixer.EndMixer(partId);
	//End participant video input/output
	videoMixer.EndMixer(partId);	
	videoMixer.EndMixer(partId+100000);

        if ( part->use.WaitUnusedAndLock(2000) != 1)
        {
            return Error("DestroyParticipant: failed lock participant %d\n", partId);
        }

	Log("-DestroyParticipant deleting mixers [%d]\n",partId);

	//QUitamos los mixers
	videoMixer.DeleteMixer(partId);
	videoMixer.DeleteMixer(partId+100000);
	
	audioMixer.DeleteMixer(partId);
	textMixer.DeleteMixer(partId);
	
	part->use.Unlock(); 
	//Delete participant
	ret &= Participant::DestroyParticipant(part);
	
	participantsLock.IncUse();
	confEmpty = ( participants.size() == 0);
	if ( confEmpty ) sharedDocMixer.StopBfcpServer();
	participantsLock.DecUse();	
	
	Log("<DestroyParticipant [%d], ret=%d\n",partId, ret);
	return ret;
}

Participant *MultiConf::GetParticipant(int partId)
{
	//Find participant
	Participants::iterator it = participants.find(partId);

	//If not found
	if (it == participants.end())
	{
		//Error
		Error("Participant %d not found\n", partId);
		return NULL;
	}

	//Get the participant
	return it->second;
}

Participant *MultiConf::GetParticipant(int partId,Participant::Type type)
{
	//Find participant
	Participant *part = GetParticipant(partId);

	//If no participant
	if (!part)
		//Exit
		return NULL;

	//Ensure it is from the correct type
	if (part->GetType()!=type)
	{
		//Error
		Error("Participant is not of desired type\n");
		return NULL;
	}

	//Return it
	return part;
}

/************************
* End
* 	Termina una multiconferencia
*************************/
int MultiConf::End()
{
	int ret = 1;
	Log(">End multiconf\n");
	

	//End watchers
	StopBroadcaster();
	StopRecordingBroadcaster();
		
	//End mixers
	audioMixer.EndMixer(watcherId);
	textMixer.EndMixer(watcherId);

	//Stop encoding
	audioEncoder.StopEncoding();
	textEncoder.StopEncoding();

	//End encoders
	audioEncoder.End();
	textEncoder.End();

	//End mixers
	audioMixer.DeleteMixer(watcherId);
	textMixer.DeleteMixer(watcherId);

	//Get lock
	participantsLock.WaitUnusedAndLock();
	
	//Destroy all participants
	for(Participants::iterator it=participants.begin(); it!=participants.end(); it++)
	{
		//Destroy it
		if ( DestroyParticipant(it->first,it->second) == 0 ) ret = 0;;
	}

	//Clear list
	participants.clear();
	
	//Unlock
	participantsLock.Unlock();
	
	
	
	//Remove all players
	while(players.size()>0)
		//Delete the first one
		DeletePlayer(players.begin()->first);
		
	sharedDocMixer.End();

	//Stop app mixer
	videoMixer.EndMixer(AppMixerId);
	videoMixer.EndMixer(SharedDocMixerId);
	//End it
	appMixer.End();

	//Delete mixer
	videoMixer.DeleteMixer(AppMixerId);
	videoMixer.DeleteMixer(SharedDocMixerId);

	Log("-End conference mixers\n");

	//Terminamos los mixers
	
	videoMixer.End();
	audioMixer.End();
	textMixer.End();

	//No inicado
	inited = 0;

	Log("<End multiconf\n");
	return ret;
}

/************************
* SetVideoCodec
* 	SetVideoCodec
*************************/
int MultiConf::SetVideoCodec(int id,int codec,int mode,int fps,int bitrate,int intraPeriod,const Properties &properties,MediaFrame::MediaRole role)
{
	int ret = 0;

	Log("-SetVideoCodec [%d]\n",id);

	//Use list
	participantsLock.IncUse();

	//Get the participant
	Participant *part = GetParticipant(id);

	//Check particpant
	if (part)
		//Set video codec
		ret = part->SetVideoCodec((VideoCodec::Type)codec,mode,fps,bitrate,intraPeriod,properties,role);

	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}

int MultiConf::SetLocalCryptoSDES(int id,MediaFrame::Type media,const char *suite,const char* key, MediaFrame::MediaRole role)
{
	int ret = 0;

	Log("-SetLocalCryptoSDES %s [partId:%d role:%s]\n",MediaFrame::TypeToString(media),id,MediaFrame::RoleToString(role));

	//Use list
	participantsLock.IncUse();

	//Get the participant
	RTPParticipant *part = (RTPParticipant*) GetParticipant(id,Participant::RTP);

	//Check particpant
	if (part)
		//Set  codec
		ret = part->SetLocalCryptoSDES(media,suite,key, role);
	else
		Error("-SetLocalCryptoSDES: participant %d does not exist or is not an RTP participant.\n", id);

	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}

int MultiConf::SetRemoteCryptoSDES(int id,MediaFrame::Type media,const char *suite,const char* key, MediaFrame::MediaRole role,int keyRank)
{
	int ret = 0;

	Log("-SetRemoteCryptoSDES %s [partId:%d role:%s]\n",MediaFrame::TypeToString(media),id,MediaFrame::RoleToString(role));

	//Use list
	participantsLock.IncUse();

	//Get the participant
	RTPParticipant *part = (RTPParticipant*)GetParticipant(id,Participant::RTP);

	//Check particpant
	if (part)
		//Set  codec
		ret = part->SetRemoteCryptoSDES(media,suite,key, role,keyRank);

	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}

int MultiConf::SetRemoteCryptoDTLS(int id,MediaFrame::Type media,const char *setup,const char *hash,const char *fingerprint)
{
	int ret = 0;


	Log("-SetRemoteCryptoDTLS %s [partId:%d]\n",MediaFrame::TypeToString(media),id);

	//Use list
	participantsLock.IncUse();

	//Get the participant
	RTPParticipant *part = (RTPParticipant*)GetParticipant(id,Participant::RTP);

	//Check particpant
	if (part)
		//Set  codec

		ret = part->SetRemoteCryptoDTLS(media,setup,hash,fingerprint);

	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}


int MultiConf::SetLocalSTUNCredentials(int id,MediaFrame::Type media,const char *username,const char* pwd, MediaFrame::MediaRole role)
{
	int ret = 0;

	Log("-SetLocalSTUNCredentials %s [partId:%d,username:%s,pwd:%s]\n",MediaFrame::TypeToString(media),id,username,pwd);

	//Use list
	participantsLock.IncUse();

	//Get the participant
	RTPParticipant *part = (RTPParticipant*)GetParticipant(id,Participant::RTP);

	//Check particpant
	if (part)
		//Set  codec
		ret = part->SetLocalSTUNCredentials(media,username,pwd, role);

	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}
int MultiConf::SetRTPProperties(int id,MediaFrame::Type media,const Properties& properties,MediaFrame::MediaRole role )
{
	int ret = 0;

	//Use list
	participantsLock.IncUse();
	Participant *part = GetParticipant(id);
	
	if (part)
	{
	    switch ( part->GetType() )
	    {
		case Participant::RTP:
			ret = ( (RTPParticipant*) part )->SetRTPProperties(media,properties,role);
			break;
			
		case Participant::RTMP:
			ret = ( (RTMPParticipant*) part )->SetCodecProperties(media,properties);
			break;
			
		default:
			Error("-SetRTPProperties: this participant type does not support properties.\n");
			ret = 0;
			break;
		}
	}

	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}

int MultiConf::SetParticipantBackground(int id, const char * filename)
{
	int ret;
	if (id <= 0 )
	{
		videoMixer.LoadLogo(filename);
		ret = 1;
	}
	else
	{
		ret = 0;
		participantsLock.IncUse();
		Participant *part = GetParticipant(id);
		if (part)
		{
			ret = part->LoadLogo(filename);
		}
		participantsLock.DecUse();
	}
	return ret;
}

int MultiConf::SetParticipantOverlay(int mosaicId, int id, const char * filename)
{
	int ret;
        if (filename != NULL && strlen(filename) > 0)
        {
	    videoMixer.SetOverlayImage(mosaicId, id, filename);
        }
        else
        {
	    videoMixer.ResetOverlayImage(mosaicId, id);
        }	
        ret = 1;
	return ret;
}


int MultiConf::SetParticipantDisplayName(int mosaicId, int partId, const char *name,int scriptCode)
{
    return videoMixer.SetDisplayName(mosaicId, partId, name,scriptCode);
}

int MultiConf::AcceptDocSharingRequest(int confId, int partId)
{
	Log(">AcceptDocSharingRequest  [partId:%d]\n",partId);

	int ret = 0 ;
	Participant *part = GetParticipant(partId);
	
	if (part)
	{
		ret = sharedDocMixer.AcceptDocSharingRequest(confId,part);
		
		//sharedDocMixer.ShareSecondaryStream(part);
	}
	return ret;
	
}

int MultiConf::RefuseDocSharingRequest(int confId,int partId)
{
	Log(">RefuseDocSharingRequest  [partId:%d]\n",partId);

	int ret = 0 ;
	Participant *part = GetParticipant(partId);
	
	if (part)
	{
		ret = sharedDocMixer.RefuseDocSharingRequest(confId,part);
		
		//sharedDocMixer.ShareSecondaryStream(part);
	}
	return ret;
	
}

int  MultiConf::StopDocSharing(int confId,int partId)
{
	int ret = 0 ;
	Participant *part = GetParticipant(partId);
	
	if (part)
	{
		ret = sharedDocMixer.StopSharing(part);
	}
	else
		ret = sharedDocMixer.StopSharing();
		
	return ret;

}

int MultiConf::SetDocSharingMosaic(int mosaicId, int id)
{
	Log(">SetDocSharingMosaic\n");
	
	int partId =0;
	RTPParticipant *part = (RTPParticipant*)GetParticipant(id,Participant::RTP);
	
	if ( mosaicId == -1 )
	{
		participantsLock.IncUse();
			
			for(Participants::iterator it=participants.begin(); it!=participants.end(); it++)
			{
				partId 	= it->first;
				part  	= (RTPParticipant*) it->second;
				
				if (part->GetDocSharingMode() == Participant::BFCP_TCP ||  part->GetDocSharingMode() == Participant::BFCP_UDP)
				{
					part->StopSending(MediaFrame::Video,MediaFrame::VIDEO_SLIDES);
				}
				
			}
		
			participantsLock.DecUse();

			
	}
	else
	{
		if (part)
		{
			videoMixer.InitMixer(id+100000,mosaicId);
			part->StartSending(MediaFrame::Video,MediaFrame::VIDEO_SLIDES);
					
		}
		else
		{
		
			participantsLock.IncUse();
			
			for(Participants::iterator it=participants.begin(); it!=participants.end(); it++)
			{
				partId 	= it->first;
				part  	= (RTPParticipant*) it->second;
				
				if (part->GetDocSharingMode() == Participant::BFCP_TCP ||  part->GetDocSharingMode() == Participant::BFCP_UDP )
				{
				
					videoMixer.InitMixer(partId+100000,mosaicId);
					part->StartSending(MediaFrame::Video,MediaFrame::VIDEO_SLIDES);
			
				}
				
			}
		
			participantsLock.DecUse();

			
		}
	}
	sharedDocMixer.SetSharedMosaic(mosaicId);
	Log("<SetDocSharingMosaic\n");
		
	return 1;


}

int MultiConf::SetRemoteSTUNCredentials(int id,MediaFrame::Type media,const char *username,const char* pwd, MediaFrame::MediaRole role)
{
	int ret = 0;

	Log("-SetRemoteSTUNCredentials %s [partId:%d,username:%s,pwd:%s]\n",MediaFrame::TypeToString(media),id,username,pwd);

	//Use list
	participantsLock.IncUse();

	//Get the participant
	RTPParticipant *part = (RTPParticipant*)GetParticipant(id,Participant::RTP);

	//Check particpant
	if (part)
		//Set  codec
		ret = part->SetRemoteSTUNCredentials(media,username,pwd, role);

	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}

int MultiConf::StartSending(int id,MediaFrame::Type media,char *sendIp,int sendPort,RTPMap& rtpMap,MediaFrame::MediaRole role)
{
	int ret = 0;

	Log("-StartSending %s [partId:%d]\n",MediaFrame::TypeToString(media),id);
	
	int mosaicId= sharedDocMixer.GetSharedMosaic();
	//Use list
	participantsLock.IncUse();

	//Get the participant
	RTPParticipant *part = (RTPParticipant*)GetParticipant(id,Participant::RTP);
	
	//Check particpant
	if (part)
	{
		//si addresse ip publique, on envoie un Hello
		if (strcmp(sendIp,"0.0.0.0") != 0 )
		{
			switch (media)
			{
				case MediaFrame::Application:
						sharedDocMixer.initDocSharing(part,sendIp,sendPort);
						break;
			}
		}
		
		//Set  codec
		ret = part->StartSending(media,sendIp,sendPort,rtpMap,role);
		if (role ==MediaFrame::VIDEO_SLIDES)
		{
			if (mosaicId >=0)
					SetDocSharingMosaic(mosaicId,id);
		}
	}
	
	
	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}

int MultiConf::StopSending(int id,MediaFrame::Type media,MediaFrame::MediaRole role)
{
	int ret = 0;

	Log("-StopSending %s [partId:%d]\n",MediaFrame::TypeToString(media),id);

	//Use list
	participantsLock.IncUse();

	//Get the participant
	RTPParticipant *part = (RTPParticipant*)GetParticipant(id,Participant::RTP);

	//Check particpant
	if (part)
		//Set video codec
		ret = part->StopSending(media,role);

	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}

int MultiConf::StartReceiving(int id,MediaFrame::Type media,RTPMap& rtpMap,MediaFrame::MediaRole role, int confId, MediaFrame::MediaProtocol proto)
{
	int ret = 0;
	Participant::DocSharingMode docSharingMode;
	
	Log("-StartReceiving %s [partId:%d]\n",MediaFrame::TypeToString(media),id);

	//Use list
	participantsLock.IncUse();

	//Get the participant
	RTPParticipant *part = (RTPParticipant*)GetParticipant(id,Participant::RTP);
	//Unlock
	participantsLock.DecUse();
	//Check participant
	if (part)
	{
		switch (media)
		{
			case MediaFrame::Application:
				//Get t140 for redundancy
				for (RTPMap::iterator it = rtpMap.begin(); it!=rtpMap.end(); ++it)
				{
					//Is it ourr codec
					if (it->second==AppCodec::BFCP)
					{
						if (proto == MediaFrame::UDP)
							docSharingMode = Participant::BFCP_UDP;
						else
							docSharingMode = Participant::BFCP_TCP;
						
						if (confId > 0)
							ret = sharedDocMixer.addParticipant(confId, part, docSharingMode,proto);
						if (ret)
							ret	= sharedDocMixer.getServerPort(part);
						continue;
					}
				}
				break;			
			default:
				ret = part->StartReceiving(media,rtpMap,role);
		}
	}
	
	Log("StartReceiving ret=%i\n",ret);	
						
	//Exit
	return ret;
}

/************************
* StopReceivingVideo
* 	StopReceivingVideo
*************************/
int MultiConf::StopReceiving(int id,MediaFrame::Type media,MediaFrame::MediaRole role)
{
	int ret = 0;

	Log("-StopReceiving %s [partId:%d ]\n",MediaFrame::TypeToString(media),id);

	//Use list
	participantsLock.IncUse();

	//Get the participant
	RTPParticipant *part = (RTPParticipant*)GetParticipant(id,Participant::RTP);

	//Check particpant
	if (part)
	{
		switch (media)
		{
			case MediaFrame::Application:
				ret	= sharedDocMixer.StopSharing(part);	
				break;			
			default:
				ret = part->StopReceiving(media,role);
		}
	}

	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}

/************************
* SetAudioCodec
* 	SetAudioCodec
*************************/
int MultiConf::SetAudioCodec(int id,int codec,const Properties& properties)
{
	int ret = 0;

	Log("-SetAudioCodec [%d]\n",id);

	//Use list
	participantsLock.IncUse();

	//Get the participant
	Participant *part = GetParticipant(id);

	//Check particpant
	if (part)
		//Set video codec
		ret = part->SetAudioCodec((AudioCodec::Type)codec,properties);
	else
		Error("-SetAudioCodec: participant %d does not exist.\n", id);

	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}

/************************
* SetTextCodec
* 	SetTextCodec
*************************/
int MultiConf::SetTextCodec(int id,int codec)
{
	int ret = 0;

	Log("-SetTextCodec [%d]\n",id);

	//Use list
	participantsLock.IncUse();

	//Get the participant
	Participant *part = GetParticipant(id);

	//Check particpant
	if (part)
		//Set video codec
		ret = part->SetTextCodec((TextCodec::Type)codec);

	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}

/************************
* SetAppCodec
* 	SetAppCodec
*************************/
int MultiConf::SetAppCodec(int confId, int id,int codec)
{
	int ret = 0;
	Participant::DocSharingMode docSharingMode;
	Log("-SetAppCodec [%d]\n",id);

	//Use list
	participantsLock.IncUse();

	//Get the participant
	Participant *part = GetParticipant(id);
	//Check participant
	if (part)
	{
		switch (codec)
		{
			case AppCodec::BFCP:
				videoMixer.CreateMixer(id+100000);
				videoMixer.InitMixer(id+100000,-1);
				part->SetVideoInput(videoMixer.GetInput(id+100000),MediaFrame::VIDEO_SLIDES);
				
				
				break;
			default:
				
				break;
		}

		ret =1;
	}
	
	
	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}

/************************
* SetParticipantMosaic
* 	Change participant mosaic
*************************/
int MultiConf::SetParticipantMosaic(int partId,int mosaicId)
{
	int ret = 0;

	Log("-SetParticipantMosaic [partId:%d,mosaic:%d]\n",partId,mosaicId);

	//Use list
	participantsLock.IncUse();

	//Get the participant
	Participant *part = GetParticipant(partId);

	//Check particpant
	if (part)
		//Set it in the video mixer
		ret =  videoMixer.SetMixerMosaic(partId,mosaicId);

	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}


/************************
* SetParticipantSidebar
* 	Change participant sidebar
*************************/
int MultiConf::SetParticipantSidebar(int partId,int sidebarId)
{
	int ret = 0;

	Log("-SetParticipantSidebar [partId:%d,sidebar:%d]\n",partId,sidebarId);

	//Use list
	participantsLock.IncUse();

	//Get the participant
	Participant *part = GetParticipant(partId);

	//Check particpant
	if (part)
		//Set it in the video mixer
		ret =  audioMixer.SetMixerSidebar(partId,sidebarId);

	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}

/************************
* CreatePlayer
* 	Create a media player
*************************/
int MultiConf::CreatePlayer(int privateId,std::wstring name)
{
	Log(">CreatePlayer [%d]\n",privateId);


	//SI no tamos iniciados pasamos
	if (!inited)
		return Error("Not inited\n");

	//Obtenemos el id
	int playerId = maxId++;

	//Le creamos un mixer
	if (!videoMixer.CreateMixer(playerId))
		return Error("Couldn't set video mixer\n");

	//Y el de audio
	if (!audioMixer.CreateMixer(playerId))
	{
		//Borramos el de video
		videoMixer.DeleteMixer(playerId);
		//Y salimos
		return Error("Couldn't set audio mixer\n");
	}

	//Add a pivate text
	if (!textMixer.CreatePrivate(playerId,privateId,name))
	{
		//Borramos el de video y audio
		videoMixer.DeleteMixer(playerId);
		audioMixer.DeleteMixer(playerId);
		//Y salimos
		return Error("Couldn't set text mixer\n");
	}

	//Create player
	MP4Player *player = new MP4Player();

	//Init
	player->Init(audioMixer.GetOutput(playerId),videoMixer.GetOutput(playerId),textMixer.GetOutput(playerId));

	//E iniciamos el mixer
	videoMixer.InitMixer(playerId,-1);
	audioMixer.InitMixer(playerId,-1);
	textMixer.InitPrivate(playerId);

	//Lo insertamos en el map
	players[playerId] = player;

	Log("<CreatePlayer [%d]\n",playerId);

	return playerId;
}
/************************
* StartPlaying
* 	Start playing the media in the player
*************************/
int MultiConf::StartPlaying(int playerId,const char* filename,bool loop)
{
	Log("-Start playing [id:%d,file:\"%s\",loop:%d]\n",playerId,filename,loop);

	//Find it
	Players::iterator it = players.find(playerId);

	//Si no esta
	if (it == players.end())
		//Not found
		return Error("-Player not found\n");

	//Play
	return it->second->Play(filename,loop);
}
/************************
* StopPlaying
* 	Stop the media playback
*************************/
int MultiConf::StopPlaying(int playerId)
{
	Log("-Stop playing [id:%d]\n",playerId);

	//Find it
	Players::iterator it = players.find(playerId);

	//Si no esta
	if (it == players.end())
		//Not found
		return Error("-Player not found\n");

	//Play
	return it->second->Stop();
}

/************************
* DeletePlayer
* 	Delete a media player
*************************/
int MultiConf::DeletePlayer(int id)
{
	Log(">DeletePlayer [%d]\n",id);


	//El iterator
	Players::iterator it = players.find(id);

	//Si no esta
	if (it == players.end())
		//Not found
		return Error("-Player not found\n");

	//LO obtenemos
	MP4Player *player = (*it).second;

	//Y lo quitamos del mapa
	players.erase(it);

	//Terminamos el audio y el video
	player->Stop();

	Log("-DeletePlayer ending mixers [%d]\n",id);

	//Paramos el mixer
	videoMixer.EndMixer(id);
	audioMixer.EndMixer(id);
	textMixer.EndPrivate(id);

	//End it
	player->End();

	//QUitamos los mixers
	videoMixer.DeleteMixer(id);
	audioMixer.DeleteMixer(id);
	textMixer.DeletePrivate(id);

	//Lo borramos
	delete player;

	Log("<DeletePlayer [%d]\n",id);

	return 1;
}

int MultiConf::StartRecordingParticipant(int partId,const char* filename)
{
	int ret = 0;
	
	Log("-StartRecordingParticipant [id:%d,name:\"%s\"]\n",partId,filename);

	//Lock
	participantsLock.IncUse();

	//Get participant
	RTPParticipant *rtp = (RTPParticipant*)GetParticipant(partId,Participant::RTP);

	//Check if
	if (!rtp)
		//End
		goto end;
	
	//Create recording
	if (!rtp->recorder.Create(filename))
		//End
		goto end;

        //Start recording
        if (!rtp->recorder.Record())
		//End
		goto end;
	
	//Set the listener for the rtp video packets
	rtp->SetMediaListener(&rtp->recorder);

	//Add the listener for audio and text frames of the watcher
	audioEncoder.AddListener(&rtp->recorder);
	textEncoder.AddListener(&rtp->recorder);

	//OK
	ret = 1;

end:
	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}

int MultiConf::StopRecordingParticipant(int partId)
{
	int ret = 0;
	
	Log("-StopRecordingParticipant [id:%d]\n",partId);

	//Lock
	participantsLock.IncUse();

	//Get rtp participant
	RTPParticipant* rtp = (RTPParticipant*)GetParticipant(partId,Participant::RTP);

	//Check participant
	if (rtp)
	{
		//Set the listener for the rtp video packets
		rtp->SetMediaListener(NULL);

		//Add the listener for audio and text frames of the watcher
		audioEncoder.RemoveListener(&rtp->recorder);
		textEncoder.RemoveListener(&rtp->recorder);

		//Stop recording
		rtp->recorder.Stop();

		//End recording
		ret = rtp->recorder.Close();
	}

	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}

int MultiConf::SendFPU(int partId)
{
	int ret = 0;
	
	Log("-SendFPU [id:%d]\n",partId);
	
	//Lock
	participantsLock.IncUse();

	//Get participant
	Participant *part = GetParticipant(partId);

	//Check participant
	if (part)
		//Send FPU
		ret = part->SendVideoFPU();

	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}

MultiConf::ParticipantStatistics* MultiConf::GetParticipantStatistic(int partId)
{
	//Create statistics map
	ParticipantStatistics *stats = new ParticipantStatistics();

	//Lock
	participantsLock.IncUse();

	//Find participant
	Participant* part = GetParticipant(partId);

	//Check participant
	if (part)
	{
		//Append
		(*stats)["audio"] = part->GetStatistics(MediaFrame::Audio);
		(*stats)["video"] = part->GetStatistics(MediaFrame::Video);
		(*stats)["text"]  = part->GetStatistics(MediaFrame::Text);
	}

	//Unlock
	participantsLock.DecUse();

	//Return stats
	return stats;
}

/********************************************************
 * SetMute
 *   Set participant mute
 ********************************************************/
int MultiConf::SetMute(int partId,MediaFrame::Type media,bool isMuted)
{
	int ret = 0;

	Log("-SetMute [id:%d,media:%d,muted:%d]\n",partId,media,isMuted);

	//Lock
	participantsLock.IncUse();

	//Get participant
	Participant *part = GetParticipant(partId);

	//Check participant
	if (part)
		//Send FPU
		ret = part->SetMute(media,isMuted);

	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}

/********************************************************
 * AddConferenceToken
 *   Add a token for conference watcher
 ********************************************************/
bool MultiConf::AddBroadcastToken(const std::wstring &token)
{
	Log("-AddBroadcastToken [token:\"%ls\"]\n",token.c_str());

	//Check if the pin is already in use
	if (tokens.find(token)!=tokens.end())
		//Broadcast not found
		return Error("Token already in use\n");

	//Add to the pin list
	tokens.insert(token);

	return true;
}
/********************************************************
 * AddParticipantInputToken
 *   Add a token for participant input
 ********************************************************/
bool  MultiConf::AddParticipantInputToken(int partId,const std::wstring &token)
{
	Log("-AddParticipantInputToken [id:%d,token:\"%ls\"]\n",partId,token.c_str());

	//Check if the pin is already in use
	if (tokens.find(token)!=tokens.end())
		//Broadcast not found
		return Error("Token already in use\n");

	//Add to the pin list
	inputTokens[token] = partId;

	return true;
}
/********************************************************
 * AddParticipantOutputToken
 *   Add a token for participant output
 ********************************************************/
bool  MultiConf::AddParticipantOutputToken(int partId,const std::wstring &token)
{
	Log("-AddParticipantOutputToken [id:%d,token:\"%ls\"]\n",partId,token.c_str());

	//Check if the pin is already in use
	if (tokens.find(token)!=tokens.end())
		//Broadcast not found
		return Error("Token already in use\n");

	//Add to the pin list
	outputTokens[token] = partId;

	return true;
}

/********************************************************
 * ConsumeBroadcastToken
 *   Check and consume a token for conference watcher
 ********************************************************/
RTMPMediaStream*  MultiConf::ConsumeBroadcastToken(const std::wstring &token)
{
	//Check token
	BroadcastTokens::iterator it = tokens.find(token);

	//Check we found one
	if (it==tokens.end())
	{
		//Error
		Error("Broadcast token not found\n");
		//Broadcast not found
		return NULL;
	}

	//Remove token
	tokens.erase(it);

	//It is valid so return encoder
	return &flvEncoder;
}

RTMPMediaStream::Listener* MultiConf::ConsumeParticipantInputToken(const std::wstring &token)
{
	//Check token
	ParticipantTokens::iterator it = inputTokens.find(token);

	//Check we found one
	if (it==inputTokens.end())
	{
		//Error
		Error("Participant token not found\n");
		//Broadcast not found
		return NULL;
	}

	//Get participant id
	int partId = it->second;

	//Remove token
	inputTokens.erase(it);

	//Get it
	Participants::iterator itPart = participants.find(partId);

	//Check if not found
	if (itPart==participants.end())
	{
		//Error
		Error("Participant not found\n");
		//Broadcast not found
		return NULL;
	}
	
	//Get it
	Participant* part = itPart->second;

	//Asert correct tipe
	if (part->GetType()!=Participant::RTMP)
	{
		//Error
		Error("Participant type not RTMP");

		//Broadcast not found
		return NULL;
	}
	
	//return it
	return (RTMPMediaStream::Listener*)(RTMPParticipant*)part;
}

RTMPParticipant* MultiConf::ConsumeParticipantOutputToken(const std::wstring &token)
{
	//Check token
	ParticipantTokens::iterator it = outputTokens.find(token);

	//Check we found one
	if (it==outputTokens.end())
	{
		//Error
		Error("Participant token not found\n");
		//Broadcast not found
		return NULL;
	}

	//Get participant id
	int partId = it->second;

	//Remove token
	outputTokens.erase(it);

	//Get it
	Participants::iterator itPart = participants.find(partId);

	//Check if not found
	if (itPart==participants.end())
	{
		//Error
		Error("Participant not found\n");
		//Broadcast not found
		return NULL;
	}

	//Get it
	Participant* part = itPart->second;

	//Asert correct tipe
	if (part->GetType()!=Participant::RTMP)
	{
		//Error
		Error("Participant not RTMP type\n");
		//Broadcast not found
		return NULL;
	}

	//return it
	return (RTMPParticipant*)part;
}

/********************************
 * NetConnection
 **********************************/
RTMPNetStream* MultiConf::CreateStream(DWORD streamId,DWORD audioCaps,DWORD videoCaps,RTMPNetStream::Listener *listener)
{
	//No stream for that url
	RTMPNetStream *stream = new NetStream(streamId,this,listener);

	//Set tag
	stream->SetTag(tag);

	//Register the sream
	RegisterStream(stream);

	//Create stream
	return stream;
}

void MultiConf::DeleteStream(RTMPNetStream *stream)
{
	//Unregister stream
	UnRegisterStream(stream);

	//Delete the stream
	delete(stream);
}

/*****************************************************
 * RTMP Broadcast session
 *
 ******************************************************/
MultiConf::NetStream::NetStream(DWORD streamId,MultiConf *conf,RTMPNetStream::Listener* listener) : RTMPNetStream(streamId,listener)
{
	//Store conf
	this->conf = conf;
	//Not opened
	opened = false;
	    part = NULL;
}

MultiConf::NetStream::~NetStream()
{
	//Close
	Close();
	//Not opened
	opened = false;
}

/***************************************
 * Play
 *	RTMP event listener
 **************************************/
void MultiConf::NetStream::doPlay(std::wstring& url,RTMPMediaStream::Listener* listener)
{
	RTMPMediaStream *stream = NULL;

	//Log
	Log("-Play stream [%d,%ls]\n",GetStreamId(),url.c_str());

	//Check  if already in use
	if (opened)
	{
		//Send error
		fireOnNetStreamStatus(RTMP::Netstream::Failed,L"Stream already in use");
		//Noting found
		Error("Stream #%d already in use\n", GetStreamId());
		//Exit
		return;
	}

	//Skip the type part "flv:" inserted by FMS
	size_t i = url.find(L":",0);

	//If not found
	if (i==std::wstring::npos)
		//from the begining
		i = 0;
	else
		//Skip
		i++;

	//Get the next "/"
	size_t j = url.find(L"/",i);

	//Check if found
	if (j==std::wstring::npos)
	{
		//Send error
		fireOnNetStreamStatus(RTMP::Netstream::Play::Failed,L"Wrong format stream name");
		//Noting found
		Error("Wrong format stream name\n");
		//Exit
		return;
	}
	//Get type
	std::wstring type = url.substr(i,j-i);

	//Get token
	std::wstring token = url.substr(j+1,url.length()-j);

	//Check app name
	if (type.compare(L"participant")==0)
	{
		//Get participant stream
		part = conf->ConsumeParticipantOutputToken(token);
		stream = part;
		//Wait for intra
		SetWaitIntra(true);
		//And rewrite
		SetRewriteTimestamps(true);
	} else if (type.compare(L"watcher")==0) {
		//Get broadcast stream
		stream = conf->ConsumeBroadcastToken(token);
		//Wait for intra
		SetWaitIntra(true);
		//And rewrite
		SetRewriteTimestamps(true);
	} else {
		//Log
		Error("-Application type name incorrect [%ls]\n",type.c_str());
		//Send error
		fireOnNetStreamStatus(RTMP::Netstream::Play::Failed,L"Type invalid");
		//Nothing done
		return;
	}

	//Check if found
	if (!stream)
	{
		//Send error
		fireOnNetStreamStatus(RTMP::Netstream::Play::StreamNotFound,L"Token invalid");
		//Exit
		return;
	}
	
	//Opened
	opened = true;
	
	//Send reseted status
	fireOnNetStreamStatus(RTMP::Netstream::Play::Reset,L"Playback reset");
	//Send play status
	fireOnNetStreamStatus(RTMP::Netstream::Play::Start,L"Playback started");

	//Add listener
	AddMediaListener(listener);
	//Attach
	Attach(stream);
}

void MultiConf::NetStream::doSeek(DWORD time)
{
	//Send status
	fireOnNetStreamStatus(RTMP::Netstream::Seek::Failed,L"Seek not supported");
}

void MultiConf::NetStream::doPause()
{
	//Send status
    if (part)
    {
        part->StopSending();
        fireOnNetStreamStatus(RTMP::Netstream::Pause::Notify,L"Paused");
    }
    else
    {
        fireOnNetStreamStatus(RTMP::Netstream::Failed,L"Pause not supported in this participant");
    }
}

void MultiConf::NetStream::doCommand(RTMPCommandMessage *cmd)
{
    if ( cmd->GetName().compare(L"NetStream.Play.InsufficientBW") == 0 )
    {
        Log("-stream [%d] is congested\n",GetStreamId());
        if (part) part->onCongestion();
    }
    else
    {
        cmd->Dump();
    }
}

void MultiConf::NetStream::doResume()
{
	//Send status
    if (part)
    {
        part->StartSending();
        fireOnNetStreamStatus(RTMP::Netstream::Unpause::Notify,L"Resumed");
    }
    else
    {
	fireOnNetStreamStatus(RTMP::Netstream::Failed,L"Resume not supported");
    }
}

/***************************************
 * Publish
 *	RTMP event listener
 **************************************/
void MultiConf::NetStream::doPublish(std::wstring& url)
{
	//Log
	Log("-Publish stream [%ls]\n",url.c_str());

	//Check  if already in use
	if (opened)
	{
		//Send error
		fireOnNetStreamStatus(RTMP::Netstream::Failed,L"Stream already in use");
		//Noting found
		Error("Stream #%d already in use\n", GetStreamId());
		//Exit
		return;
	}

	//Skip the type part "flv:" inserted by FMS
	size_t i = url.find(L":",0);

	//If not found
	if (i==std::wstring::npos)
		//from the begining
		i = 0;
	else
		//Skip
		i++;

	//Get the next "/"
	size_t j = url.find(L"/",i);

	//Check if found
	if (j==std::wstring::npos)
	{
		//Send error
		fireOnNetStreamStatus(RTMP::Netstream::Publish::BadName,L"Wrong format stream name");
		//Noting found
		Error("Wrong format stream name\n");
		//Exit
		return;
	}
	//Get type
	std::wstring type = url.substr(i,j-i);

	//Get token
	std::wstring token = url.substr(j+1,url.length()-j);

	//Check app name
	if (!type.compare(L"participant")==0)
	{
		//Log
		Error("-Application type name incorrect [%ls]\n",type.c_str());
		//Send error
		fireOnNetStreamStatus(RTMP::Netstream::Publish::BadName,L"Type invalid");
		//Nothing done
		return;
	}

	//Get participant stream
	RTMPMediaStream::Listener *listener = conf->ConsumeParticipantInputToken(token);

	//Check if found
	if (!listener)
	{
		//Send error
		fireOnNetStreamStatus(RTMP::Netstream::Publish::BadName,L"Token invalid");
		//Exit
		return;
	}

	//Opened
	opened = true;

	//Add this as listener
	AddMediaListener(listener);

	//Send publish notification
	fireOnNetStreamStatus(RTMP::Netstream::Publish::Start,L"Publish started");
}

void MultiConf::NetStream::doClose(RTMPMediaStream::Listener *listener)
{
	//Close
	Close();
}

void MultiConf::NetStream::Close()
{
	Log(">Close multiconf netstream\n");

	part = NULL;
	///Remove listener just in case
	RemoveAllMediaListeners();
	//Dettach if playing
	Detach();

	Log("<Closed\n");
}

/* Add 
int MultiConf::AppMixerWebsocketConnectRequest(int partId,WebSocket *ws,bool isPresenter)
{
	//Connect it
	return appMixer.WebsocketConnectRequest(partId,ws,isPresenter);
}
*/

void MultiConf::onRequestFPU(Participant *part)
{
	//Check listener
	if (listener)
		//Send event
		listener->onParticipantRequestFPU(this,part->GetPartId(),this->param);
}

void MultiConf::onRequestDocSharing(int partId,std::wstring status)
{

	//Check listener
	if (listener)
		//Send event
		listener->onParticipantRequestDocSharing(this,partId,status,this->param);
}

int MultiConf::AppMixerDisplayImage(const char* filename)
{
	//Display it
	return appMixer.DisplayImage(filename);
}

int  MultiConf::StartPublishing(const char* server,int port, const char* app,const char* stream)
{
	
	PublisherInfo info;
	UTF8Parser parser;

	//Parse stream name
	parser.SetString(stream);
	
	//LOg
	Log(">StartPublishing broadcast to [url=\"rtmp://%s:%d/%s\",stream=\"%ls\"\n",server,port,app,parser.GetWChar());

	//Pa porsi
	if (!inited)
		//Exit
		return Error("Multiconf not inited");
	
	//Get published id
	info.id = maxPublisherId++;

	//Store published stream name
	info.name = parser.GetWString();
	
	//Create new publisherf1722
	info.conn = new RTMPClientConnection(tag);

	//Store id as user data
	info.conn->SetUserData(info.id);

	//No stream
	info.stream = NULL;

	//Add to map
	publishers[info.id] = info;
	
	//Connect
	info.conn->Connect(server,port,app,this);

	Log("<StartPublishing broadcast [id%d]\n",info.id);

	//Return id
	return info.id;
}

int  MultiConf::StopPublishing(int id)
{
	Log("-StopPublishing broadcast [id:%d]\n",id);
	
	//Find it
	Publishers::iterator it = publishers.find(id);

	//If not found
	if (it==publishers.end())
		//Exit
		return Error("-Publisher not found\n");

	//Get info
	PublisherInfo info = it->second;

	//Check it has an stream opened
	if (info.stream)
	{
		//Un publish
		info.stream->UnPublish();
		//And close
		info.stream->Close();
	}
	//Disconnect
	info.conn->Disconnect();
	//Exit
	return 1;
}

void MultiConf::onConnected(RTMPClientConnection* conn)
{
	//Get id
	DWORD id = conn->GetUserData();
	//Log
	Log("-RTMPClientConnection connected [id:%d]\n",id);
	//Find it
	Publishers::iterator it = publishers.find(id);
	//If found
	if (it!=publishers.end())
	{
		//Get publisher info
		PublisherInfo& info = it->second;
		//Release stream
		conn->Call(L"releaseStream",new AMFNull,new AMFString(info.name));
		//Publish
		conn->Call(L"FCPublish",new AMFNull,new AMFString(info.name));
		//Create stream
		conn->CreateStream(tag);
	} else {
		Log("-RTMPClientConnection connection not found\n");
	}
}

void MultiConf::onNetStreamCreated(RTMPClientConnection* conn,RTMPClientConnection::NetStream *stream)
{
	//Get id
	DWORD id = conn->GetUserData();
	//Log
	Log("-RTMPClientConnection onNetStreamCreated [id:%d]\n",id);
	//Find it
	Publishers::iterator it = publishers.find(id);
	//If found
	if (it!=publishers.end())
	{
		//Store sream
		PublisherInfo& info = it->second;
		//Store stream
		info.stream = stream;
		//Do publish url
		stream->Publish(info.name);
		//Wait for intra
		stream->SetWaitIntra(true);
		//Add listener (TODO: move downwards)
		flvEncoder.AddMediaListener(stream);
	}
}

void MultiConf::onCommandResponse(RTMPClientConnection* conn,DWORD id,bool isError,AMFData* param)
{
	//We sould do the add listener here
}
void MultiConf::onDisconnected(RTMPClientConnection* conn)
{
	//TODO: should we lock? I expect so
	//Check if it were ended
	if (inited)
		//Do nothing it will be handled outside
		return;
	//Get id
	DWORD id = conn->GetUserData();
	//Log
	Log("-RTMPClientConnection onDisconnected [id:%d]\n",id);
	//Find it
	Publishers::iterator it = publishers.find(id);
	//If found
	if (it!=publishers.end())
	{
		//Store sream
		PublisherInfo& info = it->second;
		//If it was an stream
		if (info.stream)
		{
			//Remove listener
			flvEncoder.RemoveMediaListener((RTMPMediaStream::Listener*)info.stream);
			//Delete it
			delete(info.stream);
		}
		//Delete connection
		delete(info.conn);
		//Remove
		publishers.erase(it);
	}
}

int MultiConf::DumpInfo( std::string & info)
{
	char partInfo[200];

	if ( participants.empty() )
	{
		info += "No participant.\n";
		return 200;
	}

	info += "Participants: ";
	for (  Participants::iterator it = participants.begin();
		it != participants.end();
		it++ )
	{
		sprintf(partInfo, "%d ", it->first);
		info += partInfo;
	}
	info += "\n";
	return 200;
}
void MultiConf::SetVADMode(int mode)
{
	audioMixer.SetVADMode(mode);
	//Set vad mode
	if (mode)
		//Set VAD proxyG
		videoMixer.SetVADProxy(&audioMixer);
	videoMixer.SetVADMode((VideoMixer::VADMode) mode);
}


int MultiConf::DumpParticipantInfo(int partId, std::string & info)
{
        char partName[80];
	Participants::iterator itPart = participants.find(partId);

	//Check if not found
	if (itPart==participants.end())
	{
        	sprintf(partName, "Participant %d not found", partId);
		//Error
		info = partName;
		//Broadcast not found
		return 404;
	}

	//Get it
	Participant* part = itPart->second;
        
        sprintf(partName, "Participant %d:\n", partId);
        info = partName;
        int code = part->DumpInfo(info);
        if ( code == 200)
        {
            sprintf(partName, "  Listening to sidebar %d:\n", audioMixer.GetMixerSidebar(partId));
            info += partName;
        }

        return code;
}

 int MultiConf::DumpMixerInfo(int id, MediaFrame::Type media, std::string & info)
 {
     switch(media)
     {
         case MediaFrame::Audio:
             return audioMixer.DumpMixerInfo(id, info);

         default:
             info = "unsupported media";
             return 404;
     }
 }