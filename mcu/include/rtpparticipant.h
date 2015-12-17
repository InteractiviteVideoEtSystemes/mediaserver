/* 
 * File:   rtpparticipant.h
 * Author: Sergio
 *
 * Created on 19 de enero de 2012, 18:41
 */

#ifndef RTPPARTICIPANT_H
#define	RTPPARTICIPANT_H

#include "config.h"
#include "participant.h"
#include "videostream.h"
#include "audiostream.h"
#include "textstream.h"
#include "mp4recorder.h"


#define MAX_VIDEO_STREAM 2

class RTPParticipant : public Participant, public VideoStream::Listener
{
public:
	RTPParticipant(DWORD partId,const std::wstring &uuid);
	virtual ~RTPParticipant();

	virtual int SetVideoCodec(VideoCodec::Type codec,int mode,int fps,int bitrate,int intraPeriod,const Properties& properties,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	virtual int SetAudioCodec(AudioCodec::Type codec,const Properties& properties);
	virtual int SetTextCodec(TextCodec::Type codec);

	virtual int SendVideoFPU(MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	virtual MediaStatistics GetStatistics(MediaFrame::Type type,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);

	virtual int SetVideoInput(VideoInput* input,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN)	{  video[role]->SetVideoInput(input); return 1;	}
	virtual int SetVideoOutput(VideoOutput* output,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN) {  video[role]->SetVideoOutput(output); return 1;	}
	virtual VideoOutput* GetVideoOutput(MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN) { return video[role]->GetVideoOutput();	}
	virtual int SetAudioInput(AudioInput* input)	{ audioInput	= input;	}
	virtual int SetAudioOutput(AudioOutput *output)	{ audioOutput	= output;	}
	virtual int SetTextInput(TextInput* input)	{ textInput	= input;	}
	virtual int SetTextOutput(TextOutput* output)	{ textOutput	= output;	}

	virtual int SetMute(MediaFrame::Type media, bool isMuted ,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);

	virtual int Init();
	virtual int End();

	int StartSending(MediaFrame::Type media,char *sendIp,int sendPort,RTPMap& rtpMap,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	int StartSending(MediaFrame::Type media,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	int StopSending(MediaFrame::Type media,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	int StartReceiving(MediaFrame::Type media,RTPMap& rtpMap,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	int StartReceiving(MediaFrame::Type media,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	int StopReceiving(MediaFrame::Type media,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	int SetLocalCryptoSDES(MediaFrame::Type media,const char* suite, const char* key64,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	int SetRemoteCryptoSDES(MediaFrame::Type media,const char* suite, const char* key64,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN,int keyRank=0);
        int SetRemoteCryptoDTLS(MediaFrame::Type media,const char *setup,const char *hash,const char *fingerprint,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	int SetLocalSTUNCredentials(MediaFrame::Type media,const char* username, const char* pwd,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	int SetRemoteSTUNCredentials(MediaFrame::Type media,const char* username, const char* pwd,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	int SetRTPProperties(MediaFrame::Type media,const Properties& properties,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	
	int SetMediaListener(MediaFrame::Listener *listener,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN) { return video[role]->SetMediaListener(listener); }

	//RTPSession::Listener
	virtual void onFPURequested(RTPSession *session);
	virtual void onReceiverEstimatedMaxBitrate(RTPSession *session,DWORD bitrate);
	virtual void onTempMaxMediaStreamBitrateRequest(RTPSession *session,DWORD bitrate,DWORD overhead);
	virtual void onRequestFPU();
	virtual void onNewStream( RTPSession *session, DWORD newSsrc, bool receiving );
	
        virtual int DumpInfo(std::string & info);
		
public:
	MP4Recorder	recorder; //FIX this!
private:
	VideoStream* 	video[MAX_VIDEO_STREAM];
	AudioStream		audio;
	TextStream		text;
	RemoteRateEstimator estimator;

	AudioInput*	audioInput;
	AudioOutput*	audioOutput;
	TextInput*	textInput;
	TextOutput*	textOutput;
};

#endif	/* RTPPARTICIPANT_H */

