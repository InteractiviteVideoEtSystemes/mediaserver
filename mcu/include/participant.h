/* 
 * File:   participant.h
 * Author: Sergio
 *
 * Created on 19 de enero de 2012, 18:29
 */

#ifndef PARTICIPANT_H
#define	PARTICIPANT_H

#include "video.h"
#include "audio.h"
#include "text.h"
#include "rtpsession.h"
#include "logo.h"
#include "dtmfmessage.h"

class Participant
{
public:
	enum Type { RTP=0,RTMP=1 };
	enum DocSharingMode { NONE=0,BFCP_TCP=1,BFCP_UDP=2};

public:
	class Listener
	{
	public:
		virtual void onRequestFPU(Participant *part) = 0;
		virtual void onDTMF(Participant *part,DTMFMessage* dtmf) = 0;
	};
public:
	Participant(Type type,int partId)
	{
		this->type = type;
		this->partId = partId;
		this->docSharingMode = NONE ;
	}

	virtual ~Participant()
	{
	}
	
	Type GetType()
	{
		return type;
	}
	
	void SetListener(Listener *listener)
	{
		//Store listener
		this->listener = listener;
	}

        virtual int DumpInfo(std::string & info)
        {
            char partInfo[200];
            MediaStatistics s = GetStatistics(MediaFrame::Audio, MediaFrame::VIDEO_MAIN);

            sprintf(partInfo, 
                    "  Type=%s, DocSharing=%s.\n"
                    "  Audio: nb packets rcved %d, nb packets sent %d\n",
                    type == RTP ? "RTP" : "RTMP",
                    (docSharingMode == BFCP_TCP || docSharingMode == BFCP_UDP ) ? "BFCP" : "NONE",
                    s.numRecvPackets, s.numSendPackets);

            info += partInfo;
            return 200;
        }

	DWORD GetPartId()
	{
		return partId;
	}

	virtual int SetVideoCodec(VideoCodec::Type codec,int mode,int fps,int bitrate,int intraPeriod,const Properties &properties, MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN) = 0;
	virtual int SetAudioCodec(AudioCodec::Type codec,const Properties &properties) = 0;
	virtual int SetTextCodec(TextCodec::Type codec) = 0;
	
	
	virtual int SetVideoInput(VideoInput* input,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN) = 0;
	virtual int SetVideoOutput(VideoOutput* output,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN) = 0;
	virtual VideoOutput*  GetVideoOutput(MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN) = 0;
	virtual int SetAudioInput(AudioInput* input) = 0;
	virtual int SetAudioOutput(AudioOutput *output) = 0;
	virtual int SetTextInput(TextInput* input) = 0;
	virtual int SetTextOutput(TextOutput* output) = 0;

	virtual MediaStatistics GetStatistics(MediaFrame::Type media,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN) = 0;
	virtual int SetMute(MediaFrame::Type media, bool isMuted,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN) = 0;
	virtual int SendVideoFPU(MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN) = 0;
	virtual int SendDTMF(DTMFMessage* dtmf) = 0;
	
	virtual int Init() = 0;
	virtual int End() = 0;
	
	virtual int AcceptDocSharingRequest(int confId,int partId) 	{};
	virtual int RefuseDocSharingRequest(int confId,int partId)	{};
	virtual int StopDocSharing(int confId,int partId)			{};
	
	static bool DestroyParticipant(Participant* part)
	{
		// Wait for all threads to be stopped
		if ( part->use.WaitUnusedAndLock(2000) == 1)
                {
                    part->use.Unlock();
                    delete part;
                    return true;
                }
                return false;
	}
	
	
	int LoadLogo(const char * filename) { return logo.Load(filename); }
	void SetDocSharingMode(DocSharingMode mode) { docSharingMode = mode; }
	DocSharingMode GetDocSharingMode() { return docSharingMode; }

	Use		use;
protected:
	Type type;
	DocSharingMode docSharingMode;
	Listener *listener;
	DWORD partId;
	Logo logo;
	//Use		use;
};

#endif	/* PARTICIPANT_H */

