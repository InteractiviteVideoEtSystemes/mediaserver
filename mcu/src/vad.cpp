#include "vad.h"
#include "log.h"
#ifdef VADWEBRTC
extern "C"
{
    #include <common_audio/vad/include/webrtc_vad.h>
}

VAD::VAD()
{
	//Init webrtc vad
	WebRtcVad_InitCore(&inst);
	//Set agrresive mode
	WebRtcVad_set_mode_core(&inst,VERYAGGRESIVE);
}

int VAD::CalcVad(SWORD* frame,DWORD size,DWORD rate)
{
	//Check rates
	if ( WebRtcVad_ValidRateAndFrameLength(rate, size) != 0 )
	{
	    if (rate == 8000 || rate == 16000 || rate == 32000)
	    {
		DWORD duration = (size*1000) / rate;

		if ( duration > 20 )
		{
		    // Clip to 20 ms 
		    size = (rate/1000) * 20; 
		}
		else if ( duration > 10 )
		{
		    // Clip to 10 ms
		    size = (rate/1000) * 10;
		}
		else
		{
		    return Error("VAD: bufer is too short to be used.\n");
		}
		// DOuble check with new sie
		if ( WebRtcVad_ValidRateAndFrameLength(rate, size) != 0 )
		{
	            Error("VAD: Cannot use buffer ADJUSTED size %u for VAD at rate = %u.\n",
		      size, rate);
	            return 0;
		}
	    }
	    else
	    {
	        Error("VAD: Cannot use sample buffer size %u for VAD at rate = %u.\n",
		      size, rate);
	        return 0;
	    }
	}

	switch (rate)
	{
		case 8000:
			//Calculate VAD
			return WebRtcVad_CalcVad8khz(&inst,frame,size);
		case 16000:
			//Calculate VAD
			return WebRtcVad_CalcVad16khz(&inst,frame,size);
		case 32000:
			//Calculate VAD
			return WebRtcVad_CalcVad32khz(&inst,frame,size);
	}

	//No reate supported
	return 0;
}

bool VAD::SetMode(Mode mode)
{
	//Set mode
	return !WebRtcVad_set_mode_core(&inst,mode);
}

int VAD::GetVAD()
{
	return inst.vad;
}
#endif
