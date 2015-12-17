#ifndef _VIDEOMIXER_H_
#define _VIDEOMIXER_H_
#include <pthread.h>
#include <video.h>
#include <use.h>
#include "pipevideoinput.h"
#include "pipevideooutput.h"
#include "eventstreaminghandler.h"
#include "mosaic.h"
#include "logo.h"
#include <map>
#include <list>

class VideoMixer 
{
public:
	enum VADMode
	{
		NoVAD	 = 0,
		BasicVAD = 1,
		FullVAD  = 2
	};
public:
	// Los valores indican el n�mero de mosaicos por composicion

	VideoMixer(const std::wstring &tag);
	~VideoMixer();

	int Init(Mosaic::Type comp,int size, const char * logoFile = NULL);
	int LoadLogo(const char *filename);
	Logo* GetLogo(){ return &logo;};
	void SetVADMode(VADMode vadMode);
	void SetVADProxy(VADProxy* proxy);
	int CreateMixer(int id);
	int SetDisplayName(int mosaicId, int id, const char *name,int scriptCode);
	int InitMixer(int id,int mosaicId);
	int SetMixerMosaic(int id,int mosaicId);
	int EndMixer(int id);
	int DeleteMixer(int id);
	VideoInput*  GetInput(int id);
	VideoOutput* GetOutput(int id);
	int SetSlot(int num,int id);
	int SetCompositionType(Mosaic::Type comp,int size);

	int CreateMosaic(Mosaic::Type comp,int size);
	int SetOverlayImage(int mosaicId, int partid, const char* filename);
	int ResetOverlayImage(int mosaicId, int partid);
	int AddMosaicParticipant(int mosaicId,int partId);
	int RemoveMosaicParticipant(int mosaicId,int partId);
	int GetMosaicPositions(int mosaicId,std::list<int> &positions);
	int SetSlot(int mosaicId,int num,int id);
	int SetCompositionType(int mosaicId,Mosaic::Type comp,int size);
	int DeleteMosaic(int mosaicId);

	int End();
	
public:
	static void SetVADDefaultChangePeriod(DWORD ms);

protected:
	int MixVideo();
	int DumpMosaic(DWORD id,Mosaic* mosaic);
	int UpdateMosaic(Mosaic* mosaic);
	int GetPosition(int mosaicId,int id);
	
private:
	static void * startMixingVideo(void *par);

private:

	//Tipos
	typedef struct 
	{
		PipeVideoInput  *input;
		PipeVideoOutput *output;
		Mosaic *mosaic;
	} VideoSource;

	typedef std::map<int,VideoSource *> Videos;
	typedef std::map<int,Mosaic *> Mosaics;
private:
	static DWORD vadDefaultChangePeriod;
private:
	EvenSource	eventSource;
	std::wstring tag;
	//La lista de videos a mezclar
	Videos lstVideos;
	//Mosaics
	Mosaics mosaics;
	int maxMosaics;

	//Las propiedades del mosaico
	Logo 	logo;
	Mosaic	*defaultMosaic;

	//Threads, mutex y condiciones
	pthread_t 	mixVideoThread;
	pthread_cond_t  mixVideoCond;
	pthread_mutex_t mixVideoMutex;
	int		mixingVideo;
	QWORD		ini;
	Use		lstVideosUse;
	VADProxy*	proxy;
	VADMode		vadMode;
};

#endif
