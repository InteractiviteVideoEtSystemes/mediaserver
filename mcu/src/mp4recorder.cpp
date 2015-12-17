#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "log.h"
#include "mp4recorder.h"
#include "h264/h264.h"
#include "aacconfig.h"



mp4track::mp4track(MP4FileHandle mp4)
{
	// Set struct info
	this->mp4 = mp4;
	track = 0;
	hint = 0;
	length = 0;
	sampleId = 0;
	first = 1;
	frame = NULL;
	hasSPS = false;
	hasPPS = false;
}

int mp4track::CreateAudioTrack(AudioCodec::Type codec,DWORD rate)
{
	BYTE type;

	//Check the codec
	switch (codec)
	{
		case AudioCodec::PCMU:
		{
			// Create audio track
			track = MP4AddULawAudioTrack(mp4,rate);
			// Create audio hint track
			hint = MP4AddHintTrack(mp4, track);
			// Set payload type for hint track
			type = 0;
			MP4SetHintTrackRtpPayload(mp4, hint, "PCMU", &type, 0, NULL, 1, 0);
			// Set channel and sample properties
			MP4SetTrackIntegerProperty(mp4, track, "mdia.minf.stbl.stsd.ulaw.channels", 1);
			MP4SetTrackIntegerProperty(mp4, track, "mdia.minf.stbl.stsd.ulaw.sampleSize", 8);
			break;
		}
		break;
		case AudioCodec::PCMA:
		{
			// Create audio track
			track = MP4AddALawAudioTrack(mp4,rate);
			// Set channel and sample properties
			MP4SetTrackIntegerProperty(mp4, track, "mdia.minf.stbl.stsd.alaw.channels", 1);
			MP4SetTrackIntegerProperty(mp4, track, "mdia.minf.stbl.stsd.alaw.sampleSize", 8);
			// Create audio hint track
			hint = MP4AddHintTrack(mp4, track);
			// Set payload type for hint track
			type = 8;
			MP4SetHintTrackRtpPayload(mp4, hint, "PCMA", &type, 0, NULL, 1, 0);
			break;
		}
		break;
		case AudioCodec::AAC:
		{
			// Create audio track
			track = MP4AddAudioTrack(mp4, rate, 1024, MP4_MPEG2_AAC_LC_AUDIO_TYPE);
			//Create AAC config
			AACSpecificConfig config(rate,1);
			// Set channel and sample properties
			MP4SetTrackESConfiguration(mp4, track,config.GetData(),config.GetSize());
			// No hint track
			hint = 0;
			break;
		}
		break;
		default:
			Log("-mp4recorder: unsupported codec %s for audio track.\n", AudioCodec::GetNameFor(codec));
			return 0;
	}
	if (track > 0) Log("-mp4recorder: created audio track %d using codec %s.\n", track, AudioCodec::GetNameFor(codec));
	return track;
}

int mp4track::CreateVideoTrack(VideoCodec::Type codec,int width, int height)
{
	BYTE type;

	//Check the codec
	switch (codec)
	{
		case VideoCodec::H263_1996:
		{
			// Create video track
			track = MP4AddH263VideoTrack(mp4, 90000, 0, width, height, 0, 0, 0, 0);
			// Create video hint track
			hint = MP4AddHintTrack(mp4, track);
			// Set payload type for hint track
			type = 34;
			MP4SetHintTrackRtpPayload(mp4, hint, "H263", &type, 0, NULL, 1, 0);
			break;
		}
		case VideoCodec::H263_1998:
		{
			// Create video track
			track = MP4AddH263VideoTrack(mp4, 90000, 0, width, height, 0, 0, 0, 0);
			// Create video hint track
			hint = MP4AddHintTrack(mp4, track);
			// Set payload type for hint track
			type = 96;
			MP4SetHintTrackRtpPayload(mp4, hint, "H263-1998", &type, 0, NULL, 1, 0);
			break;
		}
		case VideoCodec::H264:
		{
			// Should parse video packet to get this values
			unsigned char AVCProfileIndication 	= 0x42;	//Baseline
			unsigned char AVCLevelIndication	= 0x0D;	//1.3
			unsigned char AVCProfileCompat		= 0xC0;
			MP4Duration h264FrameDuration		= 1.0/30;
			// Create video track
			track = MP4AddH264VideoTrack(mp4, 90000, h264FrameDuration, width, height, AVCProfileIndication, AVCProfileCompat, AVCLevelIndication,  3);
			// Create video hint track
			hint = MP4AddHintTrack(mp4, track);
			// Set payload type for hint track
			type = 99;
			MP4SetHintTrackRtpPayload(mp4, hint, "H264", &type, 0, NULL, 1, 0);
			break;
		}
		default:
		    Log("-mp4recorder: unsupported codec %s for video track.\n", VideoCodec::GetNameFor(codec));
		    break;
	}
	if (track > 0) Log("-mp4recorder: created video track %d using codec %s.\n", track, VideoCodec::GetNameFor(codec));
}

int mp4track::CreateTextTrack()
{
	//Create subtitle track
	track = MP4AddSubtitleTrack(mp4,1000,384,60);
	if (track > 0) Log("-mp4recorder: created subtitle track %d.\n", track);
}

int mp4track::FlushAudioFrame(AudioFrame* frame,DWORD duration)
{
	// Save audio frame
	MP4WriteSample(mp4, track, frame->GetData(), frame->GetLength(), duration, 0, 1);

	//If as rtp info
	if (hint)
	{
		// Add rtp hint
		MP4AddRtpHint(mp4, hint);

		///Create packet
		MP4AddRtpPacket(mp4, hint, 0, 0);

		// Set full frame as data
		MP4AddRtpSampleData(mp4, hint, sampleId, 0, frame->GetLength());

		// Write rtp hint
		MP4WriteRtpHint(mp4, hint, duration, 1);
	}

	// Delete old one
	delete frame;
	//Stored
	return 1;
}

int mp4track::WriteFrame(AudioFrame &audioFrame)
{
	//Store old one
	AudioFrame* prev = (AudioFrame*)frame;

	//Clone new one and store
	frame = audioFrame.Clone();

	//Check if we had and old frame
	if (!prev)
		//Exit
		return 0;
	
	//One more frame
	sampleId++;

	//Get number of samples
	DWORD duration = (frame->GetTimeStamp()-prev->GetTimeStamp())*audioFrame.GetRate()/1000;

	//Flush sample
	FlushAudioFrame((AudioFrame *)prev,duration);
	
	//Exit
	return 1;
}

int mp4track::FlushVideoFrame(VideoFrame* frame,DWORD duration)
{
	// Save video frame
	MP4WriteSample(mp4, track, frame->GetData(), frame->GetLength(), duration, 0, frame->IsIntra());

	//Check if we have rtp data
	if (frame->HasRtpPacketizationInfo())
	{
		//Get list
		MediaFrame::RtpPacketizationInfo& rtpInfo = frame->GetRtpPacketizationInfo();
		//Add hint for frame
		MP4AddRtpHint(mp4, hint);
		//Get iterator
		MediaFrame::RtpPacketizationInfo::iterator it = rtpInfo.begin();
		//Latest?
		bool last = (it==rtpInfo.end());

		//Iterate
		while(!last)
		{
			//Get rtp packet and move to next
			MediaFrame::RtpPacketization *rtp = *(it++);
			//is last?
			last = (it==rtpInfo.end());
			//Create rtp packet
			MP4AddRtpPacket(mp4, hint, last, 0);

			//Check rtp payload header len
			if (rtp->GetPrefixLen())
				//Add rtp data
				MP4AddRtpImmediateData(mp4, hint, rtp->GetPrefixData(), rtp->GetPrefixLen());

			//Add rtp data
			MP4AddRtpSampleData(mp4, hint, sampleId, rtp->GetPos(), rtp->GetSize());

			//It is h264 and we still do not have SPS or PPS?
			if (frame->GetCodec()==VideoCodec::H264 && (!hasSPS || !hasPPS))
			{
				//Get rtp data pointer
				BYTE *data = frame->GetData()+rtp->GetPos();
				//Check nal type
				BYTE nalType = data[0] & 0x1F;
				//Get nal data
				BYTE *nalData = data+1;
				DWORD nalSize = rtp->GetSize()-1;

				//If it a SPS NAL
				if (!hasSPS && nalType==0x07)
				{
					H264SeqParameterSet sps;
					//DEcode SPS
					sps.Decode(nalData,nalSize);
					//Dump
					sps.Dump();
					//Update widht an ehight
					MP4SetTrackIntegerProperty(mp4,track,"mdia.minf.stbl.stsd.avc1.width", sps.GetWidth());
					MP4SetTrackIntegerProperty(mp4,track,"mdia.minf.stbl.stsd.avc1.height", sps.GetHeight());
					//Add it
					MP4AddH264SequenceParameterSet(mp4,track,nalData,nalSize);
					//No need to search more
					hasSPS = true;
				}

				//If it is a PPS NAL
				if (!hasPPS && nalType==0x08)
				{
					//Add it
					MP4AddH264PictureParameterSet(mp4,track,nalData,nalSize);
					//No need to search more
					hasPPS = true;
				}
			}
		}

		//Save rtp
		MP4WriteRtpHint(mp4, hint, duration, frame->IsIntra());
	}

	// Delete old one
	delete frame;
	//Stored
	return 1;
}

int mp4track::WriteFrame(VideoFrame& videoFrame)
{
	//Store old one
	VideoFrame* prev = (VideoFrame*)frame;

	//Clone new one and store
	frame = videoFrame.Clone();

	//Check if we had and old frame
	if (!prev)
		//Exit
		return 0;

	//One mor frame
	sampleId++;

	//Get samples
	DWORD duration = (frame->GetTimeStamp()-prev->GetTimeStamp())*90;

	//Flush frame
	FlushVideoFrame(prev,duration);
	
	//Not writed
	return 1;
}

#define MAX_SUBTITLE_DURATION 7000

int mp4track::FlushTextFrame(TextFrame *frame, DWORD duration)
{
	//Set the duration of the frame on the screen
	MP4Duration frameduration = duration;

	//If it is too much
	if (frameduration>MAX_SUBTITLE_DURATION)
		//Cut it
		frameduration = MAX_SUBTITLE_DURATION;

	//Get text utf8 size
	DWORD size = frame->GetLength();

	//Create data to send
	BYTE* data = (BYTE*)malloc(size+2);

	//Set size
	data[0] = size>>8;
	data[1] = size & 0xFF;
	//Copy text
	memcpy(data+2,frame->GetData(),frame->GetLength());
	//Log
	Log("-Recording text [timestamp:%d,duration:%d, rounded duration:%d, size:%u]\n]",
	    frame->GetTimeStamp(),duration, frameduration, size+2);
	Dump(data,size+2);
	//Write sample
	MP4WriteSample( mp4, track, data, size+2, frameduration, 0, false );

	//If we have to clear the screen after 7 seconds
	if (duration > MAX_SUBTITLE_DURATION)
	{
		frameduration = duration - MAX_SUBTITLE_DURATION;
		//Log
		Log("-Recording empty text [timestamp:%d,duration:%d]\n]",
			frame->GetTimeStamp()+ MAX_SUBTITLE_DURATION,frameduration);
		//Put empty text
		data[0] = 0;
		data[1] = 0;

		//Write sample
		MP4WriteSample( mp4, track, data, 2, frameduration, 0, false );
	}
	//Free data
	free(data);
	//Stored
	return 1;
}

int mp4track::WriteFrame(TextFrame& textFrame)
{
	DWORD duration= 1000;
	//Store old one
	TextFrame* prev = (TextFrame*)frame;

	//Clone new one and store
	frame = textFrame.Clone();

	sampleId++;

	//Get samples
	if (prev!= NULL)
	{
		Log("-Recording: [prev ts:%d, new ts:%d]\n]",
		    prev->GetTimeStamp(), frame->GetTimeStamp() );
		duration = frame->GetTimeStamp()-prev->GetTimeStamp();
		FlushTextFrame( (TextFrame*) frame,duration);
		delete prev;
	}
	else
	{
		FlushTextFrame( (TextFrame*) frame ,duration);
	}


	//written
	return 1;
}

int mp4track::Close()
{
	//If we got frame
	if (frame)
	{
		//If we have pre frame
		switch (frame->GetType())
		{
			case MediaFrame::Audio:
				//Flush it
				FlushAudioFrame((AudioFrame*)frame,8000);
				break;
			case MediaFrame::Video:
				//Flush it
				FlushVideoFrame((VideoFrame*)frame,90000);
				break;
			case MediaFrame::Text:
				//Flush it
				FlushTextFrame((TextFrame*)frame,1000);
				break;
		}
		//NO frame
		frame = NULL;
	}

	return 1;
}

MP4Recorder::MP4Recorder()
{
	recording = false;
	mp4 = MP4_INVALID_FILE_HANDLE;
	audioTrack = NULL;
	videoTrack = NULL;
	textTrack = NULL;
	//Create mutex
	pthread_mutex_init(&mutex,0);
}

MP4Recorder::~MP4Recorder()
{
        //Close just in case
        Close();
        
	//If has audio track
	if (audioTrack)
		//Delete it
		delete(audioTrack);
	//If has video track
	if (videoTrack)
		//Delete it
		delete(videoTrack);
	//If has text track
	if (textTrack)
		//Delete it
		delete(textTrack);;
	//Liberamos los mutex
	pthread_mutex_destroy(&mutex);
}

bool MP4Recorder::Create(const char* filename)
{
	Log("-Opening record [%s]\n",filename);

	//If we are recording
	if (mp4!=MP4_INVALID_FILE_HANDLE)
		//Close
		Close();

	// We have to wait for first I-Frame
	waitVideo = 1;

	// Create mp4 file
	mp4 = MP4Create(filename,0);

	// If failed
	if (mp4 == MP4_INVALID_FILE_HANDLE)
                //Error
		return Error("-Error openein mp4 file for recording\n");

	//Success
	return true;
}

bool MP4Recorder::Record()
{
        //Check mp4 file is opened
        if (mp4 == MP4_INVALID_FILE_HANDLE)
                //Error
                return Error("No MP4 file opened for recording\n");
        
	//Recording
	recording = true;
	
	//Exit
	return recording;
}

bool MP4Recorder::Stop()
{
        //not recording anymore
	recording = false;
}

bool MP4Recorder::Close()
{
        //Check mp4 file is opened
        if (mp4 == MP4_INVALID_FILE_HANDLE)
                //Error
                return false;
        
        //Stop always
        Stop();

	//If has audio track
	if (audioTrack)
		//Close it
		audioTrack->Close();
	//If has video track
	if (videoTrack)
		//Close it
		videoTrack->Close();
	//If has audio track
	if (textTrack)
		//Close it
		textTrack->Close();

	// Close file
	MP4Close(mp4);

        //Empty file
        mp4 = MP4_INVALID_FILE_HANDLE;
	
	//NOthing more
	return true;
}

void MP4Recorder::onMediaFrame(MediaFrame &frame)
{
	//If not recording
	if (!recording)
		return;
	
	// Check if we have to wait for video
	if (waitVideo && (frame.GetType()!=MediaFrame::Video))
		//Do nothing yet
		return;

	//L0ck the  access to the file
	pthread_mutex_lock(&mutex);

	//Depending on the codec type
	switch (frame.GetType())
	{
		case MediaFrame::Audio:
		{
			//Convert to audio frame
			AudioFrame &audioFrame = (AudioFrame&) frame;
			// Calculate new timestamp
			QWORD timestamp = getDifTime(&first)/1000;
			// Check if we have the audio track
			if (!audioTrack)
			{
				//Create object
				audioTrack = new mp4track(mp4);
				//Create track
				audioTrack->CreateAudioTrack(audioFrame.GetCodec(),audioFrame.GetRate());
				//Create empty text frame
				AudioFrame empty(audioFrame.GetCodec(),audioFrame.GetRate());
				//Set empty data
				empty.SetTimestamp(0);
				empty.SetLength(0);
				//Set duration until first real frame
				empty.SetDuration(timestamp);
				//Send first empty packet
				audioTrack->WriteFrame(empty);
			}

			//Update timestamp
			audioFrame.SetTimestamp(timestamp);
			// Save audio rtp packet
			audioTrack->WriteFrame(audioFrame);
			break;
		}
		case MediaFrame::Video:
		{
			//Convert to video frame
			VideoFrame &videoFrame = (VideoFrame&) frame;

			//If it is intra
			if (waitVideo  && videoFrame.IsIntra())
			{
				Log("-mp4recorder: got I-frame. Starting to record media.\n");
				//Don't wait more
				waitVideo = 0;
				//Set first timestamp
				getUpdDifTime(&first);
				//And set it to be the first
				videoFrame.SetTimestamp(0);
			} else {
				// Calculate new timestamp
				QWORD timestamp = getDifTime(&first)/1000;
				//Update timestamp
				videoFrame.SetTimestamp(timestamp);
			}

			//Check if we have to write or not
			if (!waitVideo)
			{
				// Check if we have the audio track
				if (!videoTrack)
				{
					//Create object
					videoTrack = new mp4track(mp4);
					//Create track
					videoTrack->CreateVideoTrack(videoFrame.GetCodec(),videoFrame.GetWidth(),videoFrame.GetHeight());
				}

				// Save audio rtp packet
				videoTrack->WriteFrame(videoFrame);
			}
			break;
		}
		case MediaFrame::Text:
		{
			//Convert to audio frame
			TextFrame &textFrame = (TextFrame&) frame;

			// Check if we have the audio track
			if (!textTrack)
			{
				//Create object
				textTrack = new mp4track(mp4);
				//Create track
				textTrack->CreateTextTrack();
				//Create empty text frame
				TextFrame empty(0,(BYTE*)NULL,0);
				//Send first empty packet
				textTrack->WriteFrame(empty);
			}

			// Calculate new timestamp in 1000 clock
			QWORD timestamp = getDifTime(&first)/1000;
			//Update timestamp
			textFrame.SetTimestamp(timestamp);
			
			// Save text packet
			textTrack->WriteFrame(textFrame);
			break;
		}
	}

	//Unlock the  access to the file
	pthread_mutex_unlock(&mutex);
}


