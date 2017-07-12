/* 
 * File:   rtpparticipant.cpp
 * Author: Sergio
 * 
 * Created on 19 de enero de 2012, 18:41
 */

#include "rtpparticipant.h"

RTPParticipant::RTPParticipant(DWORD partId,const std::wstring &tag) :
	Participant(Participant::RTP,partId),
	audio(NULL),
	text(NULL),
	estimator(tag)
{
	Log("-RTPParticipant [id:%d,tag:%ls]\n",partId,tag.c_str());
	video[0]	=	new VideoStream(this,logo);
	video[1]	=	new VideoStream(this,logo,MediaFrame::VIDEO_SLIDES);
}

RTPParticipant::~RTPParticipant()
{
	for(int i=0; i < MAX_VIDEO_STREAM && video[i] != NULL ; ++i)
		delete(video[i]);
	
}

int RTPParticipant::SetVideoCodec(VideoCodec::Type codec,int mode,int fps,int bitrate,int intraPeriod,const Properties& properties,MediaFrame::MediaRole role)
{
	//Set it
	return video[role]->SetVideoCodec(codec,mode,fps,bitrate,intraPeriod,properties);
}

int RTPParticipant::SetAudioCodec(AudioCodec::Type codec,const Properties& properties)
{
	//Set it
	return audio.SetAudioCodec(codec,properties);
}

int RTPParticipant::SetTextCodec(TextCodec::Type codec)
{
	//Set it
	return text.SetTextCodec(codec);
}

int RTPParticipant::SendVideoFPU(MediaFrame::MediaRole role)
{
	//Send it
	return video[role]->SendFPU();
}

MediaStatistics RTPParticipant::GetStatistics(MediaFrame::Type type,MediaFrame::MediaRole role)
{
        //Depending on the type
        MediaStatistics stats;
        switch (type)
        {
                case MediaFrame::Audio:
                        stats =  audio.GetStatistics();
                        break;

                case MediaFrame::Video:
                        stats = video[role]->GetStatistics();
                        break;

                default:
                        stats = text.GetStatistics();
                        break;
        }
        Log("Stat: part %d - media %s - role %s, recv = %d, lost = %d.\n",
                partId, MediaFrame::TypeToString(type),MediaFrame::RoleToString(role),
                stats.numRecvPackets, stats.lostRecvPackets );

        return stats;
}

int RTPParticipant::End()
{
	int ret = 1;

	ret &= audio.End();
	ret &= text.End();
	
	//End all streams
	for(int i=0; i < MAX_VIDEO_STREAM && video[i] != NULL ; ++i)
		ret &= video[i]->End();
	

	//aggregater results
	return ret;
}

int RTPParticipant::Init()
{
	int ret = 1;
	
	for(int i=0; i < MAX_VIDEO_STREAM && video[i] != NULL ; ++i)
	
	{
		//Set estimator for video
		video[i]->SetRemoteRateEstimator(&estimator);
		//Init each stream
		ret &= video[i]->Init(NULL,NULL);
	}
	
	ret &= audio.Init(audioInput,audioOutput);
	ret &= text.Init(textInput,textOutput);
	//aggregater results
	return ret;
}

int RTPParticipant::SetLocalCryptoSDES(MediaFrame::Type media,const char* suite, const char* key,MediaFrame::MediaRole role )
{
	switch (media)
	{
		case MediaFrame::Audio:
			return audio.SetLocalCryptoSDES(suite,key);
		case MediaFrame::Video:
			return video[role]->SetLocalCryptoSDES(suite,key);
		case MediaFrame::Text:
			return text.SetLocalCryptoSDES(suite,key);
	}

	return 0;
}

int RTPParticipant::SetRemoteCryptoSDES(MediaFrame::Type media,const char* suite, const char* key,MediaFrame::MediaRole role,int keyRank)
{
	switch (media)
	{
		case MediaFrame::Audio:
			return audio.SetRemoteCryptoSDES(suite,key);
		case MediaFrame::Video:
			return video[role]->SetRemoteCryptoSDES(suite,key,keyRank);
			
		case MediaFrame::Text:
			return text.SetRemoteCryptoSDES(suite,key);
	}

	return 0;
}

int RTPParticipant::SetRemoteCryptoDTLS(MediaFrame::Type media,const char *setup,const char *hash,const char *fingerprint,
					MediaFrame::MediaRole role)
{
	switch (media)
	{
		case MediaFrame::Audio:
			return audio.SetRemoteCryptoDTLS(setup,hash,fingerprint);
		case MediaFrame::Video:
			return video[role]->SetRemoteCryptoDTLS(setup,hash,fingerprint);
		case MediaFrame::Text:
			return text.SetRemoteCryptoDTLS(setup,hash,fingerprint);
		default:
			return Error("Unknown media [%d]\n",media);
	}

	//OK
	return 1;
}


int RTPParticipant::SetLocalSTUNCredentials(MediaFrame::Type media,const char* username, const char* pwd,MediaFrame::MediaRole role)
{
	switch (media)
	{
		case MediaFrame::Audio:
			return audio.SetLocalSTUNCredentials(username,pwd);
		case MediaFrame::Video:
			return video[role]->SetLocalSTUNCredentials(username,pwd);
		case MediaFrame::Text:
			return text.SetLocalSTUNCredentials(username,pwd);
	}

	return 0;
}
int RTPParticipant::SetRTPProperties(MediaFrame::Type media,const Properties& properties,MediaFrame::MediaRole role)
{
	switch (media)
	{
		case MediaFrame::Audio:
			return audio.SetRTPProperties(properties);
		case MediaFrame::Video:
			return video[role]->SetRTPProperties(properties);
		case MediaFrame::Text:
			return text.SetRTPProperties(properties);
	}

	return 0;
}
int RTPParticipant::SetRemoteSTUNCredentials(MediaFrame::Type media,const char* username, const char* pwd,MediaFrame::MediaRole role)
{
	switch (media)
	{
		case MediaFrame::Audio:
			return audio.SetRemoteSTUNCredentials(username,pwd);
		case MediaFrame::Video:
			return video[role]->SetRemoteSTUNCredentials(username,pwd);
		case MediaFrame::Text:
			return text.SetRemoteSTUNCredentials(username,pwd);
	}

	return 0;
}

int RTPParticipant::StartSending(MediaFrame::Type media,char *ip, int port,RTPMap& rtpMap,MediaFrame::MediaRole role)
{
	switch (media)
	{
		case MediaFrame::Audio:
			return audio.StartSending(ip,port,rtpMap);
		case MediaFrame::Video:
			return video[role]->StartSending(ip,port,rtpMap);
		case MediaFrame::Text:
			return text.StartSending(ip,port,rtpMap);
	}

	return 0;
}

int RTPParticipant::StartSending(MediaFrame::Type media,MediaFrame::MediaRole role)
{
	switch (media)
	{
		case MediaFrame::Audio:
			return 0;
		case MediaFrame::Video:
			return video[role]->StartSending();
		case MediaFrame::Text:
			return 0;
	}

	return 0;
}

int RTPParticipant::StopSending(MediaFrame::Type media,MediaFrame::MediaRole role)
{
	switch (media)
	{
		case MediaFrame::Audio:
			return audio.StopSending();
		case MediaFrame::Video:
			return video[role]->StopSending();
		case MediaFrame::Text:
			return text.StopSending();
	}

	return 0;

}

int RTPParticipant::StartReceiving(MediaFrame::Type media,RTPMap& rtpMap,MediaFrame::MediaRole role)
{
		switch (media)
	{
		case MediaFrame::Audio:
			return audio.StartReceiving(rtpMap);
		case MediaFrame::Video:
			return video[role]->StartReceiving(rtpMap);
		case MediaFrame::Text:
			return text.StartReceiving(rtpMap);
	}

	return 0;
}

int RTPParticipant::StartReceiving(MediaFrame::Type media,MediaFrame::MediaRole role)
{
		switch (media)
	{
		case MediaFrame::Audio:
			return 0;
		case MediaFrame::Video:
			return video[role]->StartReceiving();
		case MediaFrame::Text:
			return 0;
	}

	return 0;
}

int RTPParticipant::StopReceiving(MediaFrame::Type media,MediaFrame::MediaRole role)
{
	switch (media)
	{
		case MediaFrame::Audio:
			return audio.StopReceiving();
		case MediaFrame::Video:
			return video[role]->StopReceiving();
		case MediaFrame::Text:
			return text.StopReceiving();
	}
	return 0;
}

void RTPParticipant::onFPURequested(RTPSession *session)
{
	//Request it
	video[session->GetMediaRole()]->SendFPU();
}


void RTPParticipant::onReceiverEstimatedMaxBitrate(RTPSession *session,DWORD estimation)
{
	//Limit video taking into count max audio
	video[session->GetMediaRole()]->SetTemporalBitrateLimit(estimation);
}

void RTPParticipant::onTempMaxMediaStreamBitrateRequest(RTPSession *session,DWORD estimation,DWORD overhead)
{
	//Check which session is
	if (session->GetMediaType()==MediaFrame::Video)
		//Limit video
		video[session->GetMediaRole()]->SetTemporalBitrateLimit(estimation);
}

void RTPParticipant::onRequestFPU()
{
	//Check
	if (listener)
		//Call listener
		listener->onRequestFPU(this);
}

int RTPParticipant::SetMute(MediaFrame::Type media, bool isMuted,MediaFrame::MediaRole role)
{
	//Depending on the type
	switch (media)
	{
		case MediaFrame::Audio:
			// audio
			return audio.SetMute(isMuted);
		case MediaFrame::Video:
			//Attach audio
			return video[role]->SetMute(isMuted);
		case MediaFrame::Text:
			//text
			return text.SetMute(isMuted);
	}
	return 0;
}

// Default behavior
void RTPParticipant::onNewStream( RTPSession *session, DWORD newSsrc, bool receiving )
{
	if ( ! receiving) return;

	Debug("RTPParticipant::onNewStream ssrc=%x\n",newSsrc);
	session->AddStream(receiving,newSsrc);
	
	if (GetDocSharingMode() == Participant::BFCP_TCP || GetDocSharingMode() == Participant::BFCP_UDP)
	{
		video[MediaFrame::VIDEO_SLIDES]->SetRTPSession(session,newSsrc);
	}
	
		
}

int RTPParticipant::DumpInfo(std::string& info)
{
    char partInfo[200];
    MediaStatistics s = GetStatistics(MediaFrame::Audio, MediaFrame::VIDEO_MAIN);

    sprintf(partInfo,
            "  Type=%s, DocSharing=%s.\n"
            "  Audio: nb packets rcved %d, nb packets sent %d, lost packets %d, is_receiving=%d, is_sending=%d\n",
            type == RTP ? "RTP" : "RTMP",
            (docSharingMode == BFCP_TCP || docSharingMode == BFCP_UDP )? "BFCP" : "NONE",
            s.numRecvPackets, s.numSendPackets, s.lostRecvPackets,
            audio.IsReceiving(), audio.IsSending()	);

    info += partInfo;

    s = GetStatistics(MediaFrame::Video, MediaFrame::VIDEO_MAIN);

    sprintf(partInfo,
            "  Video: nb packets rcved %d, nb packets sent %d, lost packets %d, is_receiving=%d, is_sending=%d\n",
            s.numRecvPackets, s.numSendPackets, s.lostRecvPackets,
            video[9]->IsReceiving(), video[0]->IsSending());

    info += partInfo;

    return 200;
}


