/* 
 * File:   AudioTranscoder.h
 * Author: ebuu
 *
 * Created on 7 août 2014, 00:07
 */

#include "AudioEncoderWorker.h"
#include "AudioDecoderWorker.h"
#include "codecs.h"

#ifndef AUDIOTRANSCODER_H
#define	AUDIOTRANSCODER_H

class AudioEncoderWorker;
class AudioDecoderWorker;

class AudioTranscoder :
	public Joinable,
	public Joinable::Listener
{
public:
    AudioTranscoder(const std::wstring & name);
    virtual ~AudioTranscoder();
    
    
    int Init(bool allowBriding= false);
    int End();
    int SetCodec(int c); 

    const std::wstring& GetName() { return tag;	}
    
    
	//Joinable interface
	virtual void AddListener(Joinable::Listener *listener);
	virtual void Update();
	virtual void SetREMB(DWORD estimation);
	virtual void RemoveListener(Joinable::Listener *listener);

	//Virtuals from Joinable::Listener
	virtual void onRTPPacket(RTPPacket &packet);
	virtual void onResetStream();
	virtual void onEndStream();

	//Attach
	int Attach(Joinable *join);
	int Dettach();

    
private:
    PipeAudioInput pipe;
    int state; // 0 = probbing, 1= transcoding, 2=forwarding
    int recCodec;
    bool allowBridging;
    AudioDecoderJoinableWorker decoder;
    AudioEncoderMultiplexerWorker encoder;
    std::wstring tag;
};

#endif	/* AUDIOTRANSCODER_H */

