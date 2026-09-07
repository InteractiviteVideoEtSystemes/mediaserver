#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include "log.h"
#include "multiconf.h"
#include "rtpparticipant.h"
#include "rtmpparticipant.h"
//S5 : uniquement pour la configuration globale du serveur WS (port/host/
//--websocket-secure), posée par main.cpp sur ces statiques. Le plan média
//JSR-309 de WSEndpoint n'est pas utilisé ici.
#include "jsr309/WSEndpoint.h"

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
}

void MultiConf::SetListener(Listener *listener)
{
	//Store values
	this->listener = listener;
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

	if (recorder != nullptr)
	{
		return Error("Recording is already active.\n");
	}

	//Check file name
	if (strncasecmp(ext,".flv",4)==0)
		//FLV
		recorder = std::make_unique<FLVRecorder>();
	else if (strncasecmp(ext,".mp4",4)==0)
		//MP4
		recorder = std::make_unique<MP4Recorder>();
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

	//Aligne la vidéo enregistrée sur le composite de la mosaïque enregistrée.
	//Sans cela, l'encodeur garde son défaut CIF 352x288 : une mosaïque 1280x720
	//était enregistrée en CIF, donc à la fois réduite et DÉFORMÉE (le pipe
	//redimensionne sans conserver le ratio, 16:9 écrasé en 4:3).
	int mosaicWidth = 0, mosaicHeight = 0;
	if (videoMixer.GetMosaicSize(mosaicId,mosaicWidth,mosaicHeight))
	{
		//Le débit doit suivre la taille : le défaut de FLVEncoder (512 kb/s) est
		//dimensionné pour du CIF. Appliqué tel quel à une mosaïque 1280x720 il
		//donne une image en gros pavés (~0,006 bit/pixel/image). On vise
		//0,08 bit/pixel/image, plancher au défaut historique et plafond à
		//4 Mb/s pour ne pas produire de fichiers démesurés.
		const int recFps = 30;
		int recBitrate = (int)(((QWORD)mosaicWidth*mosaicHeight*recFps*8)/100000);
		if (recBitrate < 512)  recBitrate = 512;
		if (recBitrate > 4096) recBitrate = 4096;

		Log("-Recording video size aligned on mosaic %d [%dx%d,%d fps,%d kb/s]\n",
		    mosaicId,mosaicWidth,mosaicHeight,recFps,recBitrate);
		recEncoder.SetVideoSize(mosaicWidth,mosaicHeight,recFps,recBitrate);
	}

	//Open file for recording
	if (!recorder->Create(filename))
		//Fail
		goto start_recording_failed;

	//And start recording
	if (!recorder->Record())
		//Fail
		goto start_recording_failed;

	//Check type
	switch (recorder->GetType())
	{
		case RecorderControl::FLV:
			//Set RTMP listener
			recEncoder.AddMediaListener(static_cast<FLVRecorder*>(recorder.get()));
			break;
		case RecorderControl::MP4:
			//Audio enregistré en AAC : c'est le codec audio « natif » du
			//conteneur MP4. Le G.711 (PCMA) y est certes stockable, mais seuls
			//VLC/ffmpeg le rejouent — ni les navigateurs, ni QuickTime, ni la
			//plupart des lecteurs mobiles. La piste AAC est produite par
			//FLVEncoder, qui l'ouvre à la fréquence native du mixeur audio.
			recEncoder.SetCodec(AudioCodec::AAC);
			recEncoder.AddMediaFrameListener(static_cast<MP4Recorder*>(recorder.get()));
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
	recorder.reset();
	return 0;
}

int MultiConf::StopRecordingBroadcaster()
{
	if (recorder == nullptr)
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
			recEncoder.RemoveMediaListener(static_cast<FLVRecorder*>(recorder.get()));
			break;
		case RecorderControl::MP4:
			//Set RTMP listener
			recEncoder.RemoveMediaFrameListener(static_cast<MP4Recorder*>(recorder.get()));
			break;
	}

	videoMixer.EndMixer(RecorderId);
	audioMixer.EndMixer(RecorderId);
	textMixer.EndMixer(RecorderId);



	recorder.reset();
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

	publishersLock.WaitUnusedAndLock();

	//For each publisher
	Publishers::iterator it = publishers.begin();

	//until last
	while (it!=publishers.end())
	{
		//Get first publisher info
		PublisherInfo& info = it->second;
		//Check stream
		if (info.stream)
		{
			//Un publish
			info.stream->UnPublish();
			//And close
			info.stream->Close();
			//Remove listener
			flvEncoder.RemoveMediaListener(static_cast<RTMPMediaStream::Listener*>(info.stream.get()));
		}
		//Disconnect
		info.conn->Disconnect();
		//Remove (stream/conn détruits ici par leurs unique_ptr)
		publishers.erase(it++);
	}

	publishersLock.Unlock();

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
	ParticipantPtr part;

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
			part = std::make_shared<RTPParticipant>(partId,std::wstring(uuid));
			break;
		case Participant::RTMP:
			part = std::make_shared<RTMPParticipant>(partId);
			//Create RTP Participant
			break;

		default:
			videoMixer.DeleteMixer(partId);
			audioMixer.DeleteMixer(partId);
			textMixer.DeleteMixer(partId);
			return Error("-CreateParticipant: this participant type %d is not supported.\n", type);
	}

	//Set inputs and outputs (co-propriété des pipes, Point 1 / C-4 : le
	//participant maintient le pipe vivant tant qu'il l'utilise).
	part->SetVideoInput(videoMixer.GetSharedInput(partId));
	part->SetVideoOutput(videoMixer.GetSharedOutput(partId));

	part->SetAudioInput(audioMixer.GetSharedInput(partId));
	part->SetAudioOutput(audioMixer.GetSharedOutput(partId));
	part->SetTextInput(textMixer.GetSharedInput(partId));
	part->SetTextOutput(textMixer.GetSharedOutput(partId));
	
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
    ParticipantPtr p;
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
	ParticipantPtr part = it->second;
	participantsLock.Unlock();
	
	sharedDocMixer.StopSharing(part);
    sharedDocMixer.removeParticipant(part);

	participantsLock.WaitUnusedAndLock();

	//Re-cherche par id : l'itérateur a pu être invalidé pendant la fenêtre
	//déverrouillée, et un DeleteParticipant concurrent a pu déjà le retirer.
	it = participants.find(id);
	if (it == participants.end() || it->second != part)
	{
		//Unlock
		participantsLock.Unlock();
		//Un autre thread a déjà retiré (et détruit) ce participant
		return Error("Participant already removed [%d]\n",id);
	}

	//Y lo quitamos del mapa
	participants.erase(it);

	//Unlock
	participantsLock.Unlock();

	//Destroy participatn
	int ret = DestroyParticipant(id,part);

	Log("<DeleteParticipant [%d] --> %s\n",id, ret ? "ok" : "nok");

	return ret;
}


int MultiConf::DestroyParticipant(int partId,ParticipantPtr part)
{
	Log(">DestroyParticipant [%d]\n",partId);
	bool confEmpty;

	//S5 : le pont texte-WS et ses tokens meurent avec le participant — le
	//navigateur voit son WebSocket se fermer. End() joint le thread de tirage,
	//donc hors verrou.
	std::shared_ptr<ParticipantTextWS> textWS;
	{
		std::lock_guard<std::mutex> lock(textWSMutex);
		TextWSBridges::iterator b = textWSBridges.find(partId);
		if (b != textWSBridges.end())
		{
			textWS = b->second;
			textWSBridges.erase(b);
		}
		for (TextWSTokens::iterator it = textWSTokens.begin(); it != textWSTokens.end(); )
		{
			if (it->second == (DWORD)partId)
				it = textWSTokens.erase(it);
			else
				++it;
		}
	}
	if (textWS)
		textWS->End();

	//End participant audio and video streams
	int ret = part->End();

	Log("-DestroyParticipant ending mixers [%d]\n",partId);

	//End participant audio and text mixers
	audioMixer.EndMixer(partId);
	textMixer.EndMixer(partId);
	//End participant video input/output
	videoMixer.EndMixer(partId);
	videoMixer.EndMixer(partId+100000);

	//Wait for any in-flight participant thread (e.g. RTMPParticipant::onMediaFrame) to be done
	if ( part->use.WaitUnusedAndLock(2000) != 1)
	{
		return Error("DestroyParticipant: failed lock participant %d\n", partId);
	}
	part->use.Unlock();

	Log("-DestroyParticipant deleting mixers [%d]\n",partId);

	//QUitamos los mixers
	videoMixer.DeleteMixer(partId);
	videoMixer.DeleteMixer(partId+100000);
	
	audioMixer.DeleteMixer(partId);
	textMixer.DeleteMixer(partId);
	
	
	participantsLock.IncUse();
	confEmpty = ( participants.size() == 0);
	if ( confEmpty ) sharedDocMixer.StopBfcpServer();
	participantsLock.DecUse();	
	
	Log("<DestroyParticipant [%d], ret=%d\n",partId, ret);
	return ret;
}

ParticipantPtr MultiConf::GetParticipant(int partId)
{
	//Find participant
	Participants::iterator it = participants.find(partId);

	//If not found
	if (it == participants.end())
	{
		//Error
		Error("Participant %d not found\n", partId);
		return std::shared_ptr<Participant>();
	}

	//Get the participant
	return it->second;
}

ParticipantPtr MultiConf::GetParticipant(int partId,Participant::Type type)
{
	//Find participant
	ParticipantPtr part = GetParticipant(partId);

	//If no participant
	if (!part) return part;

	//Ensure it is from the correct type
	if (part->GetType()!=type)
	{
		//Error
		Error("Participant is not of desired type\n");
		return std::shared_ptr<Participant>();
	}

	//Return it
	return part;
}

RTPParticipantPtr MultiConf::GetRTPParticipant(int partId)
{
	//Find participant
	ParticipantPtr part = GetParticipant(partId, Participant::RTP);

	//If no participant
	if (!part) return std::shared_ptr<RTPParticipant>();

	//Return it
	return std::dynamic_pointer_cast<RTPParticipant>(part);
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

	//Extrait la liste sous verrou : DestroyParticipant reprend participantsLock
	//(IncUse) et le mutex n'est pas récursif, il doit donc s'exécuter hors verrou.
	Participants parts;
	parts.swap(participants);

	//Unlock
	participantsLock.Unlock();

	//Destroy all participants (hors verrou)
	for(Participants::iterator it=parts.begin(); it!=parts.end(); it++)
	{
		//Destroy it
		if ( DestroyParticipant(it->first,it->second) == 0 ) ret = 0;
	}

	//S5 : ceinture — DestroyParticipant a déjà retiré ponts et tokens, mais
	//une conférence qui se termine ne doit rien laisser résoudre.
	{
		std::lock_guard<std::mutex> lock(textWSMutex);
		textWSTokens.clear();
		textWSBridges.clear();
	}

	
	
	//Remove all players (instantané protégé : DeletePlayer refait sa propre
	//recherche/lock, on a juste besoin d'un id valide à chaque itération)
	for(;;)
	{
		playersLock.IncUse();
		bool empty = players.empty();
		int firstId = empty ? 0 : players.begin()->first;
		playersLock.DecUse();

		if (empty)
			break;

		//Delete the first one
		DeletePlayer(firstId);
	}
		
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
	ParticipantPtr part = GetParticipant(id);

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
	RTPParticipantPtr part = GetRTPParticipant(id);

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
	RTPParticipantPtr part = GetRTPParticipant(id);

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
	RTPParticipantPtr part = GetRTPParticipant(id);

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
	RTPParticipantPtr part = GetRTPParticipant(id);

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
	ParticipantPtr part = GetParticipant(id);
	
	if (part)
	{
	    switch ( part->GetType() )
	    {
		case Participant::RTP:
			{
				RTPParticipantPtr rtpPart = std::dynamic_pointer_cast<RTPParticipant>(part);
				ret = rtpPart->SetRTPProperties(media,properties,role);
			}
			break;
			
		case Participant::RTMP:
			{
				std::shared_ptr<RTMPParticipant> rtmpPart = std::dynamic_pointer_cast<RTMPParticipant>(part);
				ret = rtmpPart->SetCodecProperties(media,properties);
			}
			break;
			
		default:
			Error("-SetRTPProperties: the participant ID %d of type %d  does not support properties.\n", 
				id, part->GetType());
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
		ParticipantPtr part = GetParticipant(id);
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
    //Le mixeur texte prend la meme etiquette : un participant a UN nom affiche,
    //pas un par media. `mosaicId` et `scriptCode` ne decrivent que le rendu du
    //bandeau video, le texte n'en a que faire. Un nom vide vide le display
    //name, et l'etiquette redevient le nom de creation.
    UTF8Parser parser;

    if (name)
        parser.Parse((BYTE*)name,strlen(name));

    textMixer.SetDisplayName(partId, parser.GetWString());

    return videoMixer.SetDisplayName(mosaicId, partId, name,scriptCode);
}

int MultiConf::AcceptDocSharingRequest(int confId, int partId)
{
	Log(">AcceptDocSharingRequest  [partId:%d]\n",partId);

	int ret = 0 ;

	//Use list (protège part contre un DeleteParticipant concurrent)
	participantsLock.IncUse();

	ParticipantPtr part = GetParticipant(partId);

	if (part)
	{
		ret = sharedDocMixer.AcceptDocSharingRequest(confId,part);

		//sharedDocMixer.ShareSecondaryStream(part);
	}

	//Desprotegemos
	participantsLock.DecUse();

	return ret;

}

int MultiConf::RefuseDocSharingRequest(int confId,int partId)
{
	Log(">RefuseDocSharingRequest  [partId:%d]\n",partId);

	int ret = 0 ;

	//Use list (protège part contre un DeleteParticipant concurrent)
	participantsLock.IncUse();

	ParticipantPtr part = GetParticipant(partId);

	if (part)
	{
		ret = sharedDocMixer.RefuseDocSharingRequest(confId,part);

		//sharedDocMixer.ShareSecondaryStream(part);
	}

	//Desprotegemos
	participantsLock.DecUse();

	return ret;

}

int  MultiConf::StopDocSharing(int confId,int partId)
{
	int ret = 0 ;

	//Use list (protège part contre un DeleteParticipant concurrent)
	participantsLock.IncUse();

	ParticipantPtr part = GetParticipant(partId);

	if (part)
	{
		ret = sharedDocMixer.StopSharing(part);
	}
	//else
	//	ret = sharedDocMixer.StopSharing();

	//Desprotegemos
	participantsLock.DecUse();

	return ret;

}

int MultiConf::SetDocSharingMosaic(int mosaicId, int id)
{
	Log(">SetDocSharingMosaic\n");

	int partId =0;

	//Use list (protège part contre un DeleteParticipant concurrent ;
	//les IncUse imbriqués des boucles ci-dessous restent valides)
	participantsLock.IncUse();

	
	RTPParticipantPtr part = GetRTPParticipant(id);

	if ( mosaicId == -1 )
	{
		participantsLock.IncUse();

			for(Participants::iterator it=participants.begin(); it!=participants.end(); it++)
			{
				partId 	= it->first;

				RTPParticipantPtr rtpPart = GetRTPParticipant(partId);
				if (!rtpPart)
					continue;

				if (rtpPart->GetDocSharingMode() == Participant::BFCP_TCP ||  rtpPart->GetDocSharingMode() == Participant::BFCP_UDP)
				{
					rtpPart->StopSending(MediaFrame::Video,MediaFrame::VIDEO_SLIDES);
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

				RTPParticipantPtr rtpPart = GetRTPParticipant(partId);
				if (!rtpPart)
					continue;

				if (rtpPart->GetDocSharingMode() == Participant::BFCP_TCP ||  rtpPart->GetDocSharingMode() == Participant::BFCP_UDP )
				{

					videoMixer.InitMixer(partId+100000,mosaicId);
					rtpPart->StartSending(MediaFrame::Video,MediaFrame::VIDEO_SLIDES);

				}

			}

			participantsLock.DecUse();


		}
	}
	sharedDocMixer.SetSharedMosaic(mosaicId);

	//Desprotegemos
	participantsLock.DecUse();

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
	RTPParticipantPtr part = GetRTPParticipant(id);

	//Check particpant
	if (part)
		//Set  codec
		ret = part->SetRemoteSTUNCredentials(media,username,pwd, role);

	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}

int MultiConf::SetAddressProfile(int id,MediaFrame::Type media,const char* profile,std::string& error,MediaFrame::MediaRole role)
{
	//Rien demandé : profil par défaut, comportement d'un contrôleur qui ignore
	//cette notion. Inutile de chercher le participant pour ne rien faire.
	if (!profile || !*profile)
		return 1;

	participantsLock.IncUse();

	RTPParticipantPtr part = GetRTPParticipant(id);
	int ret = 0;

	if (part)
		ret = part->SetAddressProfile(media,profile,error,role);
	else
		error = "participant inconnu";

	participantsLock.DecUse();

	return ret;
}

IPAddress MultiConf::GetAnnouncedAddress(int id,MediaFrame::Type media,MediaFrame::MediaRole role)
{
	participantsLock.IncUse();

	RTPParticipantPtr part = GetRTPParticipant(id);
	IPAddress addr;

	if (part)
		addr = part->GetAnnouncedAddress(media,role);

	participantsLock.DecUse();

	return addr;
}

int MultiConf::StartSending(int id,MediaFrame::Type media,char *sendIp,int sendPort,RTPMap& rtpMap,MediaFrame::MediaRole role)
{
	int ret = 0;

	Log("-StartSending %s [partId:%d]\n",MediaFrame::TypeToString(media),id);

	//S5 : le plan texte de ce participant est sur WebSocket — un StartSending
	//RTP ici ouvrirait en silence un flux que personne n'écoute (le proto est
	//ignoré pour les médias non-BFCP). Le contrôleur ne doit pas l'appeler.
	if (media == MediaFrame::Text && TextOnWebSocket(id))
		return Error("-StartSending: text is on WebSocket for participant %d, refusing RTP text.\n",id);

	int mosaicId= sharedDocMixer.GetSharedMosaic();
	//Use list
	participantsLock.IncUse();

	//Get the participant
	RTPParticipantPtr part = GetRTPParticipant(id);
	
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
	RTPParticipantPtr part = GetRTPParticipant(id);

	//Check particpant
	if (part)
		//Set video codec
		ret = part->StopSending(media,role);

	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}

int MultiConf::StartReceiving(int id,MediaFrame::Type media,RTPMap& rtpMap,MediaFrame::MediaRole role, int confId, MediaFrame::MediaProtocol proto,
                              const std::map<int,std::string>* offerFmtp,
                              std::map<int,std::string>* negotiatedFmtpOut)
{
	int ret = 0;
	Participant::DocSharingMode docSharingMode;

	Log("-StartReceiving %s [partId:%d]\n",MediaFrame::TypeToString(media),id);

	//S5 : même garde qu'au StartSending — le texte de ce participant vit sur
	//le WebSocket, pas en RTP.
	if (media == MediaFrame::Text && TextOnWebSocket(id))
		return Error("-StartReceiving: text is on WebSocket for participant %d, refusing RTP text.\n",id);

	//Use list
	participantsLock.IncUse();

	//Get the participant
	RTPParticipantPtr part = GetRTPParticipant(id);
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
				//P8a : la variante negociee n'est prise que si l'appelant veut le
				//retour enrichi. Sans elle, rien ne change pour un controleur qui
				//ne l'a pas demandee.
				if (negotiatedFmtpOut)
				{
					static const std::map<int,std::string> noFmtp;
					ret = part->StartReceiving(media,rtpMap,offerFmtp?*offerFmtp:noFmtp,*negotiatedFmtpOut,role);
				}
				else
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
/**********************
* StartRTPTimeout
*	P7/S1. Arme (timeoutMs > 0) ou desarme (0) le chien de garde d'inactivite RTP
*	d'un media. Meme prise de verrou que StopReceiving : c'est la seule chose qui
*	protege l'acces au participant.
***********************/
int MultiConf::StartRTPTimeout(int id,MediaFrame::Type media,DWORD timeoutMs,MediaFrame::MediaRole role)
{
	int ret = 0;

	Log("-StartRTPTimeout %s [partId:%d,timeoutMs:%u]\n",MediaFrame::TypeToString(media),id,timeoutMs);

	//Use list
	participantsLock.IncUse();

	//Get the participant
	RTPParticipantPtr part = GetRTPParticipant(id);

	//Check participant
	if (part)
		ret = part->StartRTPTimeout(media,timeoutMs,role);

	//Unlock
	participantsLock.DecUse();

	//Exit
	return ret;
}

int MultiConf::StopReceiving(int id,MediaFrame::Type media,MediaFrame::MediaRole role)
{
	int ret = 0;

	Log("-StopReceiving %s [partId:%d ]\n",MediaFrame::TypeToString(media),id);

	//Use list
	participantsLock.IncUse();

	//Get the participant
	RTPParticipantPtr part = GetRTPParticipant(id);

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
* ConfigureParticipantMediaConnection (S5)
*	Texte temps réel sur WebSocket pour un participant : bascule son plan
*	texte du RTP vers un pont ParticipantTextWS à la couture du mixeur, et
*	enregistre le token d'URL. Rend la base `ws(s)://host:port` (schéma
*	décidé par le serveur — décision B de jsr309_text_over_wss.md), chaîne
*	vide en cas d'échec, auquel cas l'appel RTP reste intact.
*************************/
std::string MultiConf::ConfigureParticipantMediaConnection(int partId,MediaFrame::Type media,MediaFrame::MediaProtocol proto,const std::string &token)
{
	Log(">ConfigureParticipantMediaConnection [partId:%d,media:%s,proto:%d]\n",
	    partId,MediaFrame::TypeToString(media),proto);

	if (!inited)
	{
		Error("ConfigureParticipantMediaConnection: not inited\n");
		return std::string();
	}

	//Deux transports possibles pour le texte d'un participant : le WebSocket (S5)
	//et le data channel WebRTC. Tout le reste est RTP sur cette API.
	if (media != MediaFrame::Text ||
	    (proto != MediaFrame::WS && proto != MediaFrame::SCTP))
	{
		Error("ConfigureParticipantMediaConnection: only TEXT over WS or SCTP is supported"
		      " on the conference API.\n");
		return std::string();
	}

	//Data channel : rien à signer, donc pas de token ni d'URL. La jambe garde son
	//port, son ICE et son DTLS — c'est ce qui voyage dedans qui change. Les
	//paramètres SCTP que le contrôleur publie viennent de
	//SetupParticipantDataChannel.
	if (proto == MediaFrame::SCTP)
	{
		participantsLock.IncUse();
		RTPParticipantPtr part = GetRTPParticipant(partId);
		participantsLock.DecUse();

		if (!part)
		{
			Error("ConfigureParticipantMediaConnection: participant %d not found.\n",partId);
			return std::string();
		}

		if (!part->SetTextTransport(MediaFrame::SCTP))
		{
			Error("ConfigureParticipantMediaConnection: could not switch text to a data channel.\n");
			return std::string();
		}

		Log("<ConfigureParticipantMediaConnection [partId:%d] --> data channel\n",partId);
		return std::string(MediaFrame::ProtocolToString(MediaFrame::SCTP));
	}

	if (token.empty())
	{
		Error("ConfigureParticipantMediaConnection: a token is required for WS.\n");
		return std::string();
	}

	//L'adresse d'abord : sans adresse annonçable il n'y a pas d'URL à signer,
	//et il ne faut alors RIEN basculer — le participant garde son texte RTP.
	const char* host = RTPSession::GetAnnouncedIp();
	if (WSEndpoint::GetLocalHost() && *WSEndpoint::GetLocalHost())
		host = WSEndpoint::GetLocalHost();
	if (!host || !*host)
	{
		Error("ConfigureParticipantMediaConnection: no announced address.\n");
		return std::string();
	}
	//Le SCHÉMA vient du serveur : le TLS est activé sur le MÊME port
	//(--websocket-secure), le contrôleur ne peut donc pas le deviner.
	const char* scheme = WSEndpoint::IsLocalSecure() ? "wss" : "ws";
	int port = WSEndpoint::GetLocalPort();

	//Get the participant
	participantsLock.IncUse();
	RTPParticipantPtr part = GetRTPParticipant(partId);
	participantsLock.DecUse();
	if (!part)
	{
		Error("ConfigureParticipantMediaConnection: participant %d not found.\n",partId);
		return std::string();
	}

	//Arrêter le demi-plan texte RTP (idempotent — il n'a le plus souvent
	//jamais démarré : le contrôleur configure AVANT StartReceiving). Les
	//pipes du mixeur restent vivants, le pont va les co-posséder.
	part->StopReceiving(MediaFrame::Text);
	part->StopSending(MediaFrame::Text);

	{
		std::lock_guard<std::mutex> lock(textWSMutex);

		//Un pont existe déjà (re-négociation) : on le garde — le navigateur
		//peut y être encore connecté — et seul le token change.
		if (textWSBridges.find(partId) == textWSBridges.end())
		{
			std::shared_ptr<ParticipantTextWS> bridge =
				std::make_shared<ParticipantTextWS>(textMixer.GetSharedInput(partId),
								    textMixer.GetSharedOutput(partId));
			if (!bridge->Init())
			{
				Error("ConfigureParticipantMediaConnection: could not start the text bridge.\n");
				return std::string();
			}
			textWSBridges[partId] = bridge;
		}

		//Un token par (re)configuration : l'ancien cesse de résoudre —
		//contrairement aux tokens JSR-309, jamais retirés (fuite connue,
		//risque n°3 de jsr309_text_over_wss.md).
		for (TextWSTokens::iterator it = textWSTokens.begin(); it != textWSTokens.end(); )
		{
			if (it->second == (DWORD)partId)
				it = textWSTokens.erase(it);
			else
				++it;
		}
		textWSTokens[token] = partId;
	}

	char url[128];
	if (port > 0)
		snprintf(url,sizeof(url),"%s://%s:%d",scheme,host,port);
	else
		snprintf(url,sizeof(url),"%s://%s",scheme,host);

	Log("<ConfigureParticipantMediaConnection [partId:%d] --> %s\n",partId,url);
	return std::string(url);
}

/************************
* SetupParticipantDataChannel
*	Les paramètres SCTP de la jambe texte d'un participant : le contrôleur donne
*	le `a=sctp-port` du pair et repart avec les nôtres, `a=max-message-size`
*	compris. Ce que le serveur sait de lui-même, c'est à lui qu'on le demande.
*************************/
int MultiConf::SetupParticipantDataChannel(int partId,MediaFrame::Type media,WORD remoteSCTPPort,
					   WORD& localSCTPPort,DWORD& maxMessageSize,int& streamId)
{
	if (!inited)
		return Error("SetupParticipantDataChannel: not inited\n");

	if (media != MediaFrame::Text)
		return Error("SetupParticipantDataChannel: only TEXT is carried by a data channel.\n");

	participantsLock.IncUse();
	RTPParticipantPtr part = GetRTPParticipant(partId);
	participantsLock.DecUse();

	if (!part)
		return Error("SetupParticipantDataChannel: participant %d not found.\n",partId);

	return part->SetupTextDataChannel(remoteSCTPPort,localSCTPPort,maxMessageSize,streamId);
}

/************************
* onNewMediaConnection (S5)
*	Une connexion WebSocket /mcu/<confId>/<token> résolue jusqu'à cette
*	conférence : le token désigne le pont texte d'un participant, ou 404 —
*	le miroir de MediaSession::onNewMediaConnection.
*************************/
void MultiConf::onNewMediaConnection(WebSocket *ws,const std::string &token)
{
	std::shared_ptr<ParticipantTextWS> bridge;

	{
		std::lock_guard<std::mutex> lock(textWSMutex);
		TextWSTokens::const_iterator it = textWSTokens.find(token);
		if (it != textWSTokens.end())
		{
			TextWSBridges::const_iterator b = textWSBridges.find(it->second);
			if (b != textWSBridges.end())
				bridge = b->second;
		}
	}

	if (!bridge)
	{
		Error("MultiConf::onNewMediaConnection: no such token.\n");
		ws->Reject(404,"No such token");
		return;
	}

	ws->Accept(std::weak_ptr<WebSocket::Listener>(bridge));
}

/************************
* TextOnWebSocket (S5)
*************************/
bool MultiConf::TextOnWebSocket(int partId)
{
	std::lock_guard<std::mutex> lock(textWSMutex);
	return textWSBridges.find(partId) != textWSBridges.end();
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
	ParticipantPtr part = GetParticipant(id);

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
	ParticipantPtr part = GetParticipant(id);

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
	ParticipantPtr part = GetParticipant(id);
	//Check participant
	if (part)
	{
		switch (codec)
		{
			case AppCodec::BFCP:
				videoMixer.CreateMixer(id+100000);
				videoMixer.InitMixer(id+100000,-1);
				part->SetVideoInput(videoMixer.GetSharedInput(id+100000),MediaFrame::VIDEO_SLIDES);
				
				
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
	ParticipantPtr part = GetParticipant(partId);

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
	ParticipantPtr part = GetParticipant(partId);

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
	auto player = std::make_unique<MP4Player>();

	//Init
	player->Init(audioMixer.GetOutput(playerId),videoMixer.GetOutput(playerId),textMixer.GetOutput(playerId));

	//E iniciamos el mixer
	videoMixer.InitMixer(playerId,-1);
	audioMixer.InitMixer(playerId,-1);
	textMixer.InitPrivate(playerId);

	//Lo insertamos en el map
	playersLock.WaitUnusedAndLock();
	players[playerId] = std::move(player);
	playersLock.Unlock();

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

	playersLock.IncUse();

	//Find it
	Players::iterator it = players.find(playerId);

	//Si no esta
	if (it == players.end())
	{
		playersLock.DecUse();
		//Not found
		return Error("-Player not found\n");
	}

	//Play
	int ret = it->second->Play(filename,loop);

	playersLock.DecUse();

	return ret;
}
/************************
* StopPlaying
* 	Stop the media playback
*************************/
int MultiConf::StopPlaying(int playerId)
{
	Log("-Stop playing [id:%d]\n",playerId);

	playersLock.IncUse();

	//Find it
	Players::iterator it = players.find(playerId);

	//Si no esta
	if (it == players.end())
	{
		playersLock.DecUse();
		//Not found
		return Error("-Player not found\n");
	}

	//Play
	int ret = it->second->Stop();

	playersLock.DecUse();

	return ret;
}

/************************
* DeletePlayer
* 	Delete a media player
*************************/
int MultiConf::DeletePlayer(int id)
{
	Log(">DeletePlayer [%d]\n",id);

	playersLock.WaitUnusedAndLock();

	//El iterator
	Players::iterator it = players.find(id);

	//Si no esta
	if (it == players.end())
	{
		playersLock.Unlock();
		//Not found
		return Error("-Player not found\n");
	}

	//LO obtenemos, y lo quitamos del mapa (bajo lock)
	std::unique_ptr<MP4Player> player = std::move(it->second);
	players.erase(it);

	playersLock.Unlock();

	//Terminamos el audio y el video (fuera del lock : puede unir hilos)
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

	//player se détruit ici en sortant de portée

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
	RTPParticipantPtr rtp = GetRTPParticipant(partId);

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
	RTPParticipantPtr rtp = GetRTPParticipant(partId);

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
	ParticipantPtr part = GetParticipant(partId);

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
	ParticipantPtr part = GetParticipant(partId);

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
	ParticipantPtr part = GetParticipant(partId);

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

	participantsLock.IncUse();

	//Get it
	Participants::iterator itPart = participants.find(partId);

	//Check if not found
	if (itPart==participants.end())
	{
		participantsLock.DecUse();
		//Error
		Error("Participant not found\n");
		//Broadcast not found
		return NULL;
	}

	//Get it
	ParticipantPtr part = itPart->second;

	participantsLock.DecUse();

	//Asert correct tipe (dynamic_pointer_cast fait le check ET le cast en un temps)
	std::shared_ptr<RTMPParticipant> rtmpPart = std::dynamic_pointer_cast<RTMPParticipant>(part);
	if (!rtmpPart)
	{
		//Error
		Error("Participant type not RTMP");

		//Broadcast not found
		return NULL;
	}

	//return it : pointeur brut non possédant, consommé de façon synchrone par
	//doPublish (AddMediaListener) — la durée de vie une fois enregistré comme
	//listener est déjà couverte par le mécanisme Attach/listeners de
	//RTMPMediaStream (cf. §1.4 du plan), hors périmètre C-6 (réservé phase 4
	//pour le passage générique des listeners en weak_ptr).
	return static_cast<RTMPMediaStream::Listener*>(rtmpPart.get());
}

std::weak_ptr<RTMPParticipant> MultiConf::ConsumeParticipantOutputToken(const std::wstring &token)
{
	//Check token
	ParticipantTokens::iterator it = outputTokens.find(token);

	//Check we found one
	if (it==outputTokens.end())
	{
		//Error
		Error("Participant token not found\n");
		//Broadcast not found
		return std::weak_ptr<RTMPParticipant>();
	}

	//Get participant id
	int partId = it->second;

	//Remove token
	outputTokens.erase(it);

	participantsLock.IncUse();

	//Get it
	Participants::iterator itPart = participants.find(partId);

	//Check if not found
	if (itPart==participants.end())
	{
		participantsLock.DecUse();
		//Error
		Error("Participant not found\n");
		//Broadcast not found
		return std::weak_ptr<RTMPParticipant>();
	}

	//Get it
	ParticipantPtr part = itPart->second;

	participantsLock.DecUse();

	//Asert correct tipe
	std::shared_ptr<RTMPParticipant> rtmpPart = std::dynamic_pointer_cast<RTMPParticipant>(part);
	if (!rtmpPart)
	{
		//Error
		Error("Participant not RTMP type\n");
		//Broadcast not found
		return std::weak_ptr<RTMPParticipant>();
	}

	//return it : weak_ptr non possédant, à locker au site d'usage (NetStream::part)
	return rtmpPart;
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
		//Vérrouille pour s'assurer qu'il est encore vivant et l'utiliser pour Attach()
		std::shared_ptr<RTMPParticipant> rtmpPart = part.lock();
		stream = rtmpPart.get();
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
    std::shared_ptr<RTMPParticipant> rtmpPart = part.lock();
    if (rtmpPart)
    {
        rtmpPart->StopSending();
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
        std::shared_ptr<RTMPParticipant> rtmpPart = part.lock();
        if (rtmpPart) rtmpPart->onCongestion();
    }
    else
    {
        cmd->Dump();
    }
}

void MultiConf::NetStream::doResume()
{
	//Send status
    std::shared_ptr<RTMPParticipant> rtmpPart = part.lock();
    if (rtmpPart)
    {
        rtmpPart->StartSending();
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

	part.reset();
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
		listener->onParticipantRequestFPU(this,part->GetPartId());
}

/**********************
* onParticipantMediaTimeout / onParticipantMediaConnected
*	P7/S1-S2. Simples relais vers le MCU, qui en fait des evenements de file.
*	Volontairement SANS verrou ni action sur le mix : on est appele depuis le
*	thread RTP du participant, et decider quoi faire d'une patte muette est la
*	politique du controleur SIP, pas la notre.
***********************/
void MultiConf::onParticipantMediaTimeout(Participant *part,MediaFrame::Type media,MediaFrame::MediaRole role)
{
	//Check listener
	if (listener && part)
		//Send event
		listener->onParticipantMediaTimeout(this,part->GetPartId(),media,role);
}

void MultiConf::onParticipantMediaConnected(Participant *part,MediaFrame::Type media,MediaFrame::MediaRole role)
{
	//Check listener
	if (listener && part)
		//Send event
		listener->onParticipantMediaConnected(this,part->GetPartId(),media,role);
}

void MultiConf::onDTMF(Participant * part, DTMFMessage* dtmf)
{
	//Get lock
	participantsLock.WaitUnusedAndLock();
	

	for(Participants::iterator it=participants.begin(); it!=participants.end(); it++)
	{
		//Destroy it
		//if (part->GetPartId() != it->first ) 
			it->second->SendDTMF(dtmf);
	}

	//Unlock
	participantsLock.Unlock();
	
}


void MultiConf::onRequestDocSharing(int partId,std::wstring status)
{

	//Check listener
	if (listener)
		//Send event
		listener->onParticipantRequestDocSharing(this,partId,status);
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

	//Create new publisher
	info.conn = std::make_unique<RTMPClientConnection>(tag);

	//Store id as user data
	info.conn->SetUserData(info.id);

	//No stream
	info.stream = nullptr;

	//Garder id/pointeur avant de déplacer info dans la map (move-only désormais)
	int id = info.id;
	RTMPClientConnection* conn = info.conn.get();

	//Add to map
	publishersLock.WaitUnusedAndLock();
	publishers[id] = std::move(info);
	publishersLock.Unlock();

	//Connect
	conn->Connect(server,port,app,this);

	Log("<StartPublishing broadcast [id%d]\n",id);

	//Return id
	return id;
}

int  MultiConf::StopPublishing(int id)
{
	Log("-StopPublishing broadcast [id:%d]\n",id);

	publishersLock.IncUse();

	//Find it
	Publishers::iterator it = publishers.find(id);

	//If not found
	if (it==publishers.end())
	{
		publishersLock.DecUse();
		//Exit
		return Error("-Publisher not found\n");
	}

	//Get info
	PublisherInfo& info = it->second;

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

	publishersLock.DecUse();

	//Exit
	return 1;
}

void MultiConf::onConnected(RTMPClientConnection* conn)
{
	//Get id
	DWORD id = conn->GetUserData();
	//Log
	Log("-RTMPClientConnection connected [id:%d]\n",id);

	publishersLock.IncUse();

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

	publishersLock.DecUse();
}

void MultiConf::onNetStreamCreated(RTMPClientConnection* conn,RTMPClientConnection::NetStream *stream)
{
	//Get id
	DWORD id = conn->GetUserData();
	//Log
	Log("-RTMPClientConnection onNetStreamCreated [id:%d]\n",id);

	publishersLock.IncUse();

	//Find it
	Publishers::iterator it = publishers.find(id);
	//If found
	if (it!=publishers.end())
	{
		//Store sream
		PublisherInfo& info = it->second;
		//Store stream (prend possession)
		info.stream.reset(stream);
		//Do publish url
		stream->Publish(info.name);
		//Wait for intra
		stream->SetWaitIntra(true);
		//Add listener (TODO: move downwards)
		flvEncoder.AddMediaListener(stream);
	}

	publishersLock.DecUse();
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

	publishersLock.WaitUnusedAndLock();

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
			flvEncoder.RemoveMediaListener(static_cast<RTMPMediaStream::Listener*>(info.stream.get()));
		}
		//Remove (stream/conn détruits ici par leurs unique_ptr)
		publishers.erase(it);
	}

	publishersLock.Unlock();
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
	
	ParticipantPtr part = GetParticipant(partId);

	//Check if not found
	if (!part)
	{
        	sprintf(partName, "Participant %d not found", partId);
		//Error
		info = partName;
		//Broadcast not found
		return 404;
	}

	//Get it
	
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
