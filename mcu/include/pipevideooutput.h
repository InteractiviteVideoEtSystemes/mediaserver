#ifndef _PIPVIDEOOUTPUT_H_
#define _PIPVIDEOOUTPUT_H_
#include <pthread.h>
#include <video.h>

class PipeVideoOutput :
	public VideoOutput
{
public:
	PipeVideoOutput(pthread_mutex_t* mutex, pthread_cond_t* cond);
	~PipeVideoOutput();

	virtual int NextFrame(BYTE *pic);
	virtual int SetVideoSize(int width,int height);

	BYTE*	GetFrame();
	int	IsChanged(DWORD version);
	int 	GetWidth()	{ return videoWidth;		};
	int 	GetHeight()	{ return videoHeight;		};
	int	Init();
	int	End();

	bool	SizeHasChanged(DWORD version);

private:
	BYTE*	buffer;
	int	bufferSize;
	int 	videoWidth;
	int	videoHeight;
	bool	isChanged;
	bool	versionChanged;
	bool	sizeChange;
	int 	inited;
	DWORD	version;

	pthread_mutex_t* videoMixerMutex;
	pthread_cond_t*  videoMixerCond;
};

#endif
