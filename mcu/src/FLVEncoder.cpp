#include <stdlib.h>
#include <sys/types.h>
#include <wctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include "log.h"
#include "FLVEncoder.h"
#include "flv.h"
#include "flv1/flv1codec.h"
#include "audioencoder.h"
#include "aacconfig.h"

FLVEncoder::FLVEncoder()
{
	//Not inited
	inited = 0;
	encodingAudio = 0;
	encodingVideo = 0;
	sendFPU = false;
	//Set default codecs
	audioCodec = AudioCodec::NELLY11;
	videoCodec = VideoCodec::H264;
	//Add aac properties
	audioProperties.SetProperty("aac.samplerate","48000");
	audioProperties.SetProperty("aac.bitrate","128000");
	//Set values for default video
	width	= GetWidth(CIF);
	height	= GetHeight(CIF);
	bitrate = 512;
	fps	= 30;
	intra	= 600;
	//No meta no desc
	meta = NULL;
	frameDesc = NULL;
	aacSpecificConfig = NULL;
	//Mutex
	pthread_mutex_init(&mutex,0);
	pthread_cond_init(&cond,0);
}

FLVEncoder::~FLVEncoder()
{
	//Check
	if (inited)
		//End it
		End();

	if (meta)
		delete(meta);
	if (frameDesc)
		delete(frameDesc);
	if (aacSpecificConfig)
		delete(aacSpecificConfig);

	//Mutex
	pthread_mutex_destroy(&mutex);
	pthread_cond_destroy(&cond);
}

int FLVEncoder::Init(AudioInput* audioInput,VideoInput *videoInput, TextInput *textInput)
{
	//Check if inited
	if (inited)
		//Error
		return 0;
	
	//Store inputs
	this->audioInput = audioInput;
	this->videoInput = videoInput;
	textEncoder.Init(textInput);

	//Y aun no estamos mandando nada
	encodingAudio = 0;
	encodingVideo = 0;
	//We are initer
	inited = 1;

	return 1;

}

int FLVEncoder::End()
{
	//if inited
	if (inited)
	{
		//Remove all rmpt media listenere
		RTMPMediaStream::RemoveAllMediaListeners();
		//Clear media listeners
		mediaListeners.clear();
		//Stop Encodings
		StopEncoding();
		textEncoder.End();
		//Not inited
		inited = 0;
	}
	// Wait for all threads to be stopped
	use.WaitUnusedAndLock();
	use.Unlock();
	

	return 1;
}

/***************************************
* startencodingAudio
*	Helper function
***************************************/
void * FLVEncoder::startEncodingAudio(void *par)
{
	FLVEncoder *enc = (FLVEncoder *)par;
	blocksignals();
	Log("Encoding FLV audio [%d]\n",getpid());
	pthread_exit((void *)enc->EncodeAudio());
}

/***************************************
* startencodingAudio
*	Helper function
***************************************/
void * FLVEncoder::startEncodingVideo(void *par)
{
	FLVEncoder *enc = (FLVEncoder *)par;
	blocksignals();
	Log("Encoding FLV video [%d]\n",getpid());
	pthread_exit((void *)enc->EncodeVideo());
}

DWORD FLVEncoder::AddMediaListener(RTMPMediaStream::Listener *listener)
{
	//Call parent
	RTMPMediaStream::AddMediaListener(listener);
	//Init
	listener->onStreamBegin(RTMPMediaStream::id);

	//If we already have metadata
	if (meta)
		//Send it
		listener->onMetaData(RTMPMediaStream::id,meta);
	//Check desc
	if (frameDesc)
		//Send it
		listener->onMediaFrame(RTMPMediaStream::id,frameDesc);
	//Check audio desc
	if (aacSpecificConfig)
		//Send it
		listener->onMediaFrame(RTMPMediaStream::id,aacSpecificConfig);
	//Send FPU
	sendFPU = true;
}

DWORD FLVEncoder::AddMediaFrameListener(MediaFrame::Listener* listener)
{
	//Lock mutexk
	pthread_mutex_lock(&mutex);
	//Apend
	mediaListeners.insert(listener);
	//Get number of listeners
	DWORD num = listeners.size();
	//Unlock
	textEncoder.AddListener(listener);
	pthread_mutex_unlock(&mutex);
	//return number of listeners
	return num;
}

DWORD FLVEncoder::RemoveMediaFrameListener(MediaFrame::Listener* listener)
{
	//Lock mutexk
	pthread_mutex_lock(&mutex);
	//Find it
	MediaFrameListeners::iterator it = mediaListeners.find(listener);
	//If present
	if (it!=mediaListeners.end())
		//erase it
		mediaListeners.erase(it);
	//Get number of listeners
	DWORD num = mediaListeners.size();
	textEncoder.RemoveListener(listener);
	//Unlock
	pthread_mutex_unlock(&mutex);
	//return number of listeners
	return num;
}
/***************************************
* StartEncoding
*	Comienza a mandar a la ip y puertos especificados
***************************************/
int FLVEncoder::StartEncoding()
{
	Log(">Start encoding FLV\n");

	//Si estabamos mandando tenemos que parar
	if (encodingAudio || encodingVideo)
		//paramos
		StopEncoding();

	//We are enconding
	encodingAudio = 1;
	encodingVideo = 1;

	//Set init time
	getUpdDifTime(&first);

	//Check if got old meta
	if (meta)
		//Delete
		delete(meta);

	//Create metadata object
	meta = new RTMPMetaData(0);

	//Set name
	meta->AddParam(new AMFString(L"@setDataFrame"));
	//Set name
	meta->AddParam(new AMFString(L"onMetaData"));

	//Create properties string
	AMFEcmaArray *prop = new AMFEcmaArray();

	//Set audio properties
	switch(audioCodec)
	{
		case AudioCodec::SPEEX16:
			prop->AddProperty(L"audiocodecid"	,(float)RTMPAudioFrame::SPEEX		);	//Number Audio codec ID used in the file (see E.4.2.1 for available SoundFormat values)
			prop->AddProperty(L"audiosamplerate"	,(float)16000.0				);	// Number Frequency at which the audio stream is replayed
			break;
		case AudioCodec::NELLY11:
			prop->AddProperty(L"audiocodecid"	,(float)RTMPAudioFrame::NELLY		);	//Number Audio codec ID used in the file (see E.4.2.1 for available SoundFormat values)
			prop->AddProperty(L"audiosamplerate"	,(float)11025.0				);	// Number Frequency at which the audio stream is replayed
			break;
		case AudioCodec::NELLY8:
			prop->AddProperty(L"audiocodecid"	,(float)RTMPAudioFrame::NELLY8khz	);	//Number Audio codec ID used in the file (see E.4.2.1 for available SoundFormat values)
			prop->AddProperty(L"audiosamplerate"	,(float)8000.0				);	// Number Frequency at which the audio stream is replayed
			break;
	}

	prop->AddProperty(L"stereo"		,new AMFBoolean(false)		);	// Boolean Indicating stereo audio
	prop->AddProperty(L"audiodelay"		,0.0				);	// Number Delay introduced by the audio codec in seconds
	
	//Set video codecs
	if (videoCodec==VideoCodec::SORENSON)
		//Set number
		prop->AddProperty(L"videocodecid"	,(float)RTMPVideoFrame::FLV1	);	// Number Video codec ID used in the file (see E.4.3.1 for available CodecID values)
	else if (videoCodec==VideoCodec::H264)
		//AVC
		prop->AddProperty(L"videocodecid"	,new AMFString(L"avc1")		);	// Number Video codec ID used in the file (see E.4.3.1 for available CodecID values)
	prop->AddProperty(L"framerate"		,(float)fps			);	// Number Number of frames per second
	prop->AddProperty(L"height"		,(float)height			);	// Number Height of the video in pixels
	prop->AddProperty(L"videodatarate"	,(float)bitrate			);	// Number Video bit rate in kilobits per second
	prop->AddProperty(L"width"		,(float)width			);	// Number Width of the video in pixels
	prop->AddProperty(L"canSeekToEnd"	,new AMFBoolean(false)		);	// Boolean Indicating the last video frame is a key frame

	//Add param
	meta->AddParam(prop);

	//Send metadata
	SendMetaData(meta);

	//Start audio thread
	createPriorityThread(&encodingAudioThread,startEncodingAudio,this,1);
	//Start video thread
	createPriorityThread(&encodingVideoThread,startEncodingVideo,this,1);
	//Start text thread
	textEncoder.StartEncoding();

	Log("<Start encoding FLV [%d]\n",encodingAudio);

	return 1;
}

/***************************************
* StopEncoding
* 	Termina el envio
****************************************/
int FLVEncoder::StopEncoding()
{
	Log(">Stop Encoding FLV\n");

	//Esperamos a que se cierren las threads de envio
	if (encodingAudio)
	{
		//paramos
		encodingAudio=0;

		//Y esperamos
		Log("-Stop Encoding FLV audio\n");
		//Cancel grab audio
		if ( audioInput != NULL)
			audioInput->CancelRecBuffer();
		pthread_join(encodingAudioThread,NULL);
	}

	//Esperamos a que se cierren las threads de envio
	if (encodingVideo)
	{
		//paramos
		encodingVideo=0;
		Log("-Stop Encoding FLV video\n");
		//Cancel frame cpature
		if ( videoInput != NULL)
			videoInput->CancelGrabFrame();
		//Y esperamos
		pthread_join(encodingVideoThread,NULL);	
	}
	
	//Esperamos a que se cierren las threads de text
	textEncoder.StopEncoding();
	
	Log("<Stop Encoding FLV\n");

	return 1;
}

/*******************************************
* Encode
*	Capturamos el audio y lo mandamos
*******************************************/
int FLVEncoder::EncodeAudio()
{
	Log(">Encode Audio\n");
	use.IncUse();
	
	//Create encoder
	AudioEncoder *encoder = AudioCodecFactory::CreateEncoder(audioCodec,audioProperties);

	//Check
	if (!encoder)
		//Error
		return Error("Error encoding audio");
	
	//Try to set native rate
	DWORD rate = encoder->TrySetRate(audioInput->GetNativeRate());

	//Create audio frame
	AudioFrame frame(audioCodec,rate);

	//Start recording
	audioInput->StartRecording(rate);

	//Get first
	QWORD ini = 0;

	//Num of samples since ini
	QWORD samples = 0;

	//Allocate samlpes
	SWORD* recBuffer = (SWORD*) malloc(encoder->numFrameSamples*sizeof(SWORD));

	//Check codec
	if (audioCodec==AudioCodec::AAC)
	{
		//Create AAC config frame
		aacSpecificConfig = new RTMPAudioFrame(0,AACSpecificConfig(rate,1));

		//Lock
		pthread_mutex_lock(&mutex);
		//Send audio desc
		SendMediaFrame(aacSpecificConfig);
		//unlock
		pthread_mutex_unlock(&mutex);
	}

	//Mientras tengamos que capturar
	while(encodingAudio)
	{
		//Audio frame
		RTMPAudioFrame	audio(0,65535);

		//Capturamos
		DWORD  recLen = audioInput->RecBuffer(recBuffer,encoder->numFrameSamples);
		//Check len
		if (!recLen)
		{
			//Log
			Debug("-cont\n");
			//Reser timestam
			ini = getDifTime(&first)/1000;
			//And samples
			samples = 0;
			//Skip
			continue;
		}
		
		//Rencode it
		DWORD len;

		while((len=encoder->Encode(recBuffer,recLen,audio.GetMediaData(),audio.GetMaxMediaSize()))>0)
		{
			//REset
			recLen = 0;

			//Set length
			audio.SetMediaSize(len);

			//Check if it is first frame
			if (!ini)
				//Get initial timestamp
				ini = getDifTime(&first)/1000;

			switch(encoder->type)
			{
				case AudioCodec::SPEEX16:
					//Set RTMP data
					audio.SetAudioCodec(RTMPAudioFrame::SPEEX);
					audio.SetSoundRate(RTMPAudioFrame::RATE11khz);
					audio.SetSamples16Bits(1);
					audio.SetStereo(0);
					//Set timestamp
					audio.SetTimestamp(ini+samples/16);
					//Increase samples
					samples += 320;
					break;
				case AudioCodec::NELLY8:
					//Set RTMP data
					audio.SetAudioCodec(RTMPAudioFrame::NELLY8khz);
					audio.SetSoundRate(RTMPAudioFrame::RATE11khz);
					audio.SetSamples16Bits(1);
					audio.SetStereo(0);
					//Set timestamp
					audio.SetTimestamp(ini+samples/8);
					//Increase samples
					samples += 256;
					break;
				case AudioCodec::NELLY11:
					//Set RTMP data
					audio.SetAudioCodec(RTMPAudioFrame::NELLY);
					audio.SetSoundRate(RTMPAudioFrame::RATE11khz);
					audio.SetSamples16Bits(1);
					audio.SetStereo(0);
					//Set timestamp
					audio.SetTimestamp(ini+samples*1000/11025);
					//Increase samples
					samples += 256;
					break;
				case AudioCodec::AAC:
					//Set RTMP data
					// If the SoundFormat indicates AAC, the SoundType should be 1 (stereo) and the SoundRate should be 3 (44 kHz).
					// However, this does not mean that AAC audio in FLV is always stereo, 44 kHz data.
					// Instead, the Flash Player ignores these values and extracts the channel and sample rate data is encoded in the AAC bit stream.
					audio.SetAudioCodec(RTMPAudioFrame::AAC);
					audio.SetSoundRate(RTMPAudioFrame::RATE44khz);
					audio.SetSamples16Bits(1);
					audio.SetStereo(1);
					audio.SetAACPacketType(RTMPAudioFrame::AACRaw);
					//Set timestamp
					audio.SetTimestamp(ini+samples*1000/encoder->GetClockRate());
					//Increase samples
					samples += encoder->numFrameSamples;
			}

			//Lock
			pthread_mutex_lock(&mutex);
			//Send audio
			SendMediaFrame(&audio);
			//Copy to rtp frame
			frame.SetMedia(audio.GetMediaData(),audio.GetMediaSize());
			//Set frame time
			frame.SetTimestamp(audio.GetTimestamp());
			//Set frame duration
			frame.SetDuration(encoder->numFrameSamples);
			//Clear rtp
			frame.ClearRTPPacketizationInfo();
			//Add rtp packet
			frame.AddRtpPacket(0,len,NULL,0);
			//For each listere
			//For each listener
			for(MediaFrameListeners::iterator it = mediaListeners.begin(); it!=mediaListeners.end(); ++it)
				//Send it
				(*it)->onMediaFrame(frame);
			//unlock
			pthread_mutex_unlock(&mutex);
		}
	}

	//Stop recording
	audioInput->StopRecording();

	//Delete buffer
	if (recBuffer)
		//Delete
		free(recBuffer);

	//Check codec
	if (encoder)
		//Borramos el codec
		delete(encoder);
	
	//Salimos
        Log("<Encode Audio\n");

	use.DecUse();
	//Exit
	pthread_exit(0);
}

int FLVEncoder::EncodeVideo()
{
	timeval prev;
	use.IncUse();
	//Allocate media frame
	RTMPVideoFrame frame(0,262143);

	//Check codec
	switch(videoCodec)
	{
		case VideoCodec::SORENSON:
			//Ser Video codec
			frame.SetVideoCodec(RTMPVideoFrame::FLV1);
			break;
		case VideoCodec::H264:
			//Ser Video codec
			frame.SetVideoCodec(RTMPVideoFrame::AVC);
			//Set NAL type
			frame.SetAVCType(RTMPVideoFrame::AVCNALU);
			//No delay
			frame.SetAVCTS(0);
			break;
		default:
			return Error("-Wrong codec type %d\n",videoCodec);
	}
	
	//Create the encoder
	VideoEncoder *encoder = VideoCodecFactory::CreateEncoder(videoCodec);

	///Set frame rate
	encoder->SetFrameRate(fps,bitrate,intra);

	//Set dimensions
	encoder->SetSize(width,height);

	//Start capturing
	videoInput->StartVideoCapture(width,height,fps);

	//No wait for first
	DWORD frameTime = 0;

	//Mientras tengamos que capturar
	while(encodingVideo)
	{
		//Nos quedamos con el puntero antes de que lo cambien
		BYTE* pic=videoInput->GrabFrame(frameTime);

		//Check pic
		if (!pic)
			continue;

		//Check if we need to send intra
		if (sendFPU)
		{
			//Set it
			encoder->FastPictureUpdate();
			//Do not send anymore
			sendFPU = false;
		}

		//Encode next frame
		VideoFrame *encoded = encoder->EncodeFrame(pic,videoInput->GetBufferSize());

		//Check
		if (!encoded)
			break;

		//Check size
		if (frame.GetMaxMediaSize()<encoded->GetLength())
		{
			//Not enougth space
			Error("Not enought space to copy FLV encodec frame [frame:%d,encoded:%d",frame.GetMaxMediaSize(),encoded->GetLength());
			//NExt
			continue;
		}

		//Check
		if (frameTime)
		{
			timespec ts;
			//Lock
			pthread_mutex_lock(&mutex);
			//Calculate timeout
			calcAbsTimeout(&ts,&prev,frameTime);
			//Wait next or stopped
			int canceled  = !pthread_cond_timedwait(&cond,&mutex,&ts);
			//Unlock
			pthread_mutex_unlock(&mutex);
			//Check if we have been canceled
			if (canceled)
				//Exit
				break;
		}
		//Set sending time of previous frame
		getUpdDifTime(&prev);

		//Set timestamp
		encoded->SetTimestamp(getDifTime(&first)/1000);

		//Set next one
		frameTime = 1000/fps;

		//Set duration
		encoded->SetDuration(frameTime);
		
		//Get full frame
		frame.SetVideoFrame(encoded->GetData(),encoded->GetLength());

		//Set buffer size
		frame.SetMediaSize(encoded->GetLength());

		//Check type
		if (encoded->IsIntra())
			//Set type
			frame.SetFrameType(RTMPVideoFrame::INTRA);
		else
			//Set type
			frame.SetFrameType(RTMPVideoFrame::INTER);

	
		//If we need desc but yet not have it
		if (!frameDesc && encoded->IsIntra() && videoCodec==VideoCodec::H264)
		{
			//Create new description
			AVCDescriptor desc;
			//Set values
			desc.SetConfigurationVersion(1);
			desc.SetAVCProfileIndication(0x42);
			desc.SetProfileCompatibility(0x80);
			desc.SetAVCLevelIndication(0x0C);
			desc.SetNALUnitLength(3);
			//Get encoded data
			BYTE *data = encoded->GetData();
			//Get size
			DWORD size = encoded->GetLength();
			//get from frame
			desc.AddParametersFromFrame(data,size);
			//Crete desc frame
			frameDesc = new RTMPVideoFrame(getDifTime(&first)/1000,desc);
			//Lock
			pthread_mutex_lock(&mutex);
			//Send it
			SendMediaFrame(frameDesc);
			//unlock
			pthread_mutex_unlock(&mutex);
		}
		
		
		//Set timestamp
		frame.SetTimestamp(encoded->GetTimeStamp());
		//Publish it
		SendMediaFrame(&frame);
		//For each listener
		for(MediaFrameListeners::iterator it = mediaListeners.begin(); it!=mediaListeners.end(); ++it)
			//Send it
			(*it)->onMediaFrame(*encoded);
		//unlock
		pthread_mutex_unlock(&mutex);
	}

	//Stop the capture
	videoInput->StopVideoCapture();

	//Check
	if (encoder)
		//Exit
		delete(encoder);
	
	
	use.DecUse();
	//Exit
	return 1;
}

#if 0
inline bool IsWcharPrintable(wchar_t c)
{

    // Esc seq
    if (c == 0x9b )
	return false;

    if (c >=0x20 && c < 0x0250)
    {
	// Ascii and european language
        return true;
    }
    else if ( ( c >= 0x0600 && c < 0x0780)
              ||
              ( c >= 0x08A0 && c < 0x0900) )
    {
	// Arabic
        return true;
    }
    else if ( c >= 0x0400 && c < 0x0530 )
    {
	// CYRILLIC
        return true;
    }
    else if ( c >= 0x3040 && c < 0x30A0 )
    {
	// Hiragana
        return true;
    }
    else if ( c >= 0x30A0 && c < 0x3100 )
    {
	// Katakana
        return true;
    }
    else if ( c >= 0x31F0 && c < 0x3400 )
    {
	//Katakana
         return true;
    }
    else if ( c >= 0x3400 && c < 0xA000 )
    {
	// Chinese (Han)
        return true;
    }
    else
    {
        return false;
    }
}


#define STATE_NORMAL_CHAR	0
#define STATE_WORD_SEP		1
#define STATE_SENTENCE_SEP	2
#define STATE_SENTENCE_SEP_CONF 3
#define STATE_CR_WAIT_LF	5
#define STATE_ESC_SEQ		10
int FLVEncoder::EncodeText()
{
	Log(">Encode Text\n");
	use.IncUse();
	
	//Get first
	QWORD ini = 0;
	bool inactivity = false;
	timeval first;
	std::wstring textBuffer;
	std::wstring::iterator it = textBuffer.end();
	int state = STATE_NORMAL_CHAR;
	getUpdDifTime(&first);
	//Mientras tengamos que capturar
	while(encodingText)
	{
	    TextFrame *f = textInput->GetFrame(2000);
	    if (f != NULL)
	    {
		 
		DWORD len = f->GetWLength();
		const wchar_t * t = f->GetWChar();
		inactivity = false;
#ifdef DEBUG_TEXT 
	         if ( iswprint( t[0] ) )
		    Log("-FLVEncoder got text [%ls] in state %d.\n", t, state );
		else
		    Log("-FLVEncoder got text [%02x] in state %d.\n", t[0], state );
#endif
		// Separator detection
		for (int i=0; i<len; i++)
		{

		    switch ( state )
		    {
			case STATE_CR_WAIT_LF:
			        FlushText(getDifTime(&first)/1000, textBuffer);
				it = textBuffer.end();
				state = STATE_NORMAL_CHAR;
				if (t[i] == 0x0a) break;
				// no break on purpose to proecss char
				
		        case STATE_NORMAL_CHAR:

			    if ( t[i] == ' ' )
			    {
			        FlushText(getDifTime(&first)/1000, textBuffer, false);
				state = STATE_WORD_SEP;
			    }
			    else if (t[i] == '?' || t[i] == '.' || t[i] == '!' || t[i] == ',')
				state = STATE_SENTENCE_SEP;
			    else if ( t[i] == 0x009B)
			        state = STATE_ESC_SEQ;
			    else if ( t[i] == 0x2028 || t[i] == 0x2029 || t[i] == 0x0a)
			    {
			        FlushText(getDifTime(&first)/1000, textBuffer);
				it = textBuffer.end();
			    }
			    else if ( t[i] == 0x0d )
			        state = STATE_CR_WAIT_LF;
			    else if ( t[i] == '[' )
			    {
			        FlushText(getDifTime(&first)/1000, textBuffer);
				it = textBuffer.end();
			    }
			    			    
			    if ( t[i] == 0x009B) state = STATE_ESC_SEQ; 
			    break;
			    
			case STATE_WORD_SEP:
			    if ( t[i] == 0x2028 || t[i] == 0x2029 || t[i] == 0x0a)
			    {
				FlushText(getDifTime(&first)/1000, textBuffer);
				it = textBuffer.end();
				state = STATE_NORMAL_CHAR;
			    }
			    else if ( t[i] == ' '  ) 
			        state = STATE_WORD_SEP;
			    else if ( t[i] == '?' || t[i] == '.' || t[i] == '!' || t[i] == ',')
				state = STATE_SENTENCE_SEP;
			    else if ( t[i] == 0x009B) 
				state = STATE_ESC_SEQ; 
			    else
				state = STATE_NORMAL_CHAR;
			    break;
			
			case STATE_SENTENCE_SEP:
			    if ( t[i] == 0x2028 || t[i] == 0x2029 || t[i] == 0x0a) 
			    {
				FlushText(getDifTime(&first)/1000, textBuffer);
				it = textBuffer.end();
				state = STATE_NORMAL_CHAR;
			    }
			    else if ( t[i] == ' '  )
			    {
				FlushText(getDifTime(&first)/1000, textBuffer);
				it = textBuffer.end();
				state = STATE_NORMAL_CHAR;
			    }
			    else if ( t[i] == '?' || t[i] == '.' || t[i] == '!' || t[i] == ',')
				state = STATE_SENTENCE_SEP;
			    else if ( t[i] == 0x009B) 
				state = STATE_ESC_SEQ;
			    else
				state = STATE_NORMAL_CHAR;
			    break;
			    
			case STATE_SENTENCE_SEP_CONF:
			    state = STATE_NORMAL_CHAR;
			    break;
			
			case STATE_ESC_SEQ:
			    // We are in an esc sequence. Ignore everything but the end of ESC
			    if ( t[i] == 0x006D)
				state = STATE_NORMAL_CHAR;
			    break;
			    
			default:
			    break;
			
		    }
		    
		    // Cleanly ignore ESC sequences
		    if ( state != STATE_ESC_SEQ )
		    {		    
		        if ( IsWcharPrintable( t[i] ) )
			{
			    //textBuffer.insert(it, t[i]);
				textBuffer.append(1, t[i] );
			}
			else if (t[i] == 0x08 )
			{
				if ( ! textBuffer.empty() )
				{
					std::wstring del(textBuffer);
					del += L" <--";
					
					textBuffer.erase( textBuffer.length() - 1, 1);
					FlushText(getDifTime(&first)/1000, del, false);
				}

			}    

		   	if ( textBuffer.length() > 40 && state == STATE_WORD_SEP )
		    	{
				FlushText(getDifTime(&first)/1000, textBuffer);
				it = textBuffer.end();
		    	}	
		    }
		}	
	    }	    
	    else
	    {
		if (! textBuffer.empty() )
		{
		    // If nowne type - display last text then erase everything.
		    if ( ! inactivity )
		    	FlushText(getDifTime(&first)/1000, textBuffer, false);
		    else
		    	FlushText(getDifTime(&first)/1000, textBuffer, true);
		    it = textBuffer.end();
		    inactivity = true;
		}
	    }
		
	}
	use.DecUse();
	//Exit
	return 1;
	
}

void FLVEncoder::FlushText(DWORD ts, std::wstring & text, bool clearText)
{
     TextFrame encoded(ts, text);
   
     Log("-FLVEncoder record text %ls, ts:%u\n", text.c_str(), ts); 
     for(MediaFrameListeners::iterator it = mediaListeners.begin(); it!=mediaListeners.end(); ++it)
	//Send it
	(*it)->onMediaFrame(encoded);

    if (clearText) text.clear();	
}
#endif
