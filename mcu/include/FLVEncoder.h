#ifndef _FLVENCODER_H_
#define _FLVENCODER_H_
#include <pthread.h>
#include "video.h"
#include "text.h"
#include "textencoder.h"
#include "audio.h"
#include "codecs.h"
#include "rtmpstream.h"

class FLVEncoder : public RTMPMediaStream
{
public:
	FLVEncoder();
	~FLVEncoder();
	void SetCodec( AudioCodec::Type codec )
	{
	    audioCodec = codec;
	}
	
	int Init(AudioInput* audioInput,VideoInput *videoInput, TextInput *textInput);
	int StartEncoding();
	int StopEncoding();
	int End();
	/* Overrride from RTMPMediaStream*/
	virtual DWORD AddMediaListener(RTMPMediaStream::Listener* listener);
	//Add listenest for media stream
	virtual DWORD AddMediaFrameListener(MediaFrame::Listener* listener);
	virtual DWORD RemoveMediaFrameListener(MediaFrame::Listener* listener);

protected:
	int EncodeAudio();
	int EncodeVideo();

private:
	//Funciones propias
	static void *startEncodingAudio(void *par);
	static void *startEncodingVideo(void *par);

private:
	typedef std::set<MediaFrame::Listener*> MediaFrameListeners;

	
private:
	AudioCodec::Type	audioCodec;
	AudioInput*		audioInput;
	pthread_t		encodingAudioThread;
	int			encodingAudio;
	Properties		audioProperties;

	VideoCodec::Type	videoCodec;
	VideoInput*		videoInput;
	pthread_t		encodingVideoThread;
	int			encodingVideo;

	// Text is entirely managed by text encoder
	TextEncoder		textEncoder;
	
	RTMPMetaData*	meta;
	RTMPVideoFrame* frameDesc;
	RTMPAudioFrame* aacSpecificConfig;
	int		width;
	int		height;
	int		bitrate;
	int		fps;
	int		intra;


	int		inited;
	bool		sendFPU;
	timeval		first;
	pthread_mutex_t mutex;
	pthread_cond_t	cond;
	
	Use		use;

	MediaFrameListeners mediaListeners;
	
};

#endif
