#ifndef _MP4RECORDER_H_
#define _MP4RECORDER_H_

#include <mp4v2/mp4v2.h>

#include "config.h"
#include "codecs.h"
#include "audio.h"
#include "video.h"
#include "text.h"
#include "media.h"
#include "recordercontrol.h"

class mp4track
{
public:
	mp4track(MP4FileHandle mp4);
	int CreateAudioTrack(AudioCodec::Type codec,DWORD rate);
	int CreateVideoTrack(VideoCodec::Type codec,int width, int height);
	int CreateTextTrack();
	int WriteFrame(AudioFrame &audioFrame);
	int WriteFrame(VideoFrame &videoFrame);
	int WriteFrame(TextFrame &textFrame);
	int Close();
private:
	int FlushAudioFrame(AudioFrame* frame,DWORD duration);
	int FlushVideoFrame(VideoFrame* frame,DWORD duration);
	int FlushTextFrame(TextFrame* frame,DWORD duration);
private:
	MP4FileHandle mp4;
	MP4TrackId track;
	MP4TrackId hint;
	bool first;
	bool intra;
	int length;
	int sampleId;
	MediaFrame *frame;
	bool hasSPS;
	bool hasPPS;
};


class MP4Recorder :
	public RecorderControl,
	public MediaFrame::Listener
{
public:
	MP4Recorder();
	~MP4Recorder();

	//Recorder interface
	virtual bool Create(const char *filename);
	virtual bool Record();
	virtual bool Stop();
	virtual bool Close();

	virtual RecorderControl::Type GetType()	{ return RecorderControl::MP4;	}

	virtual void onMediaFrame(MediaFrame &frame);
private:

	MP4FileHandle	mp4;
	mp4track*	audioTrack;
	mp4track*	videoTrack;
	mp4track*	textTrack;
	bool		recording;
	int		waitVideo;
	pthread_mutex_t mutex;
	timeval		first;
};
#endif
