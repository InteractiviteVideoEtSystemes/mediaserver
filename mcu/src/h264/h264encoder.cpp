#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "log.h"
#include "h264encoder.h"


#define X264_REOPEN 1
//////////////////////////////////////////////////////////////////////////
//Encoder
// 	Codificador H264
//
//////////////////////////////////////////////////////////////////////////

static void X264_log(void *p, int level, const char *fmt, va_list args)
{
	static char line[1024];
	//Create line
	vsnprintf(line,1024,fmt,args);
	//Remove underflow
	if (strstr(line,"VBV underflow")!=NULL)
		//Not show it
		return;
	//Print it
	Log("x264 %s",line);
}

/**********************
* H264Encoder
*	Constructor de la clase
***********************/
H264Encoder::H264Encoder(const Properties& properties)
{
	// Set default values
	type    = VideoCodec::H264;
	format  = 0;
	frame	= NULL;
	pts	= 0;

	//No estamos abiertos
	opened = false;

	//No bitrate
	bitrate = 0;
	fps = 0;
	intraPeriod = 0;

	h264ProfileLevelId = properties.GetProperty("h264.profile-level-id",std::string("42801F"));
	intraRefresh = (bool) properties.GetProperty("h264.intra_refresh",0);
	qPel =  properties.GetProperty("h264.qpel",3);
	//Reste values
	enc = NULL;
}

/**********************
* ~H264Encoder
*	Destructor
***********************/
H264Encoder::~H264Encoder()
{
	//If we have an encoder
	if (enc)
		//Close it
		x264_encoder_close(enc);
	//If we have created a frame
	if (frame)
		//Delete it
		delete(frame);
}

/**********************
* SetSize
*	Inicializa el tama�o de la imagen a codificar
***********************/



int H264Encoder::SetSize(int width, int height)
{
	Log("H264Encoder: SetSize [%d,%d]\n",width,height);

        if (width <= 0 || height <= 0 || (width%2) == 1 )
                return Error("H264Encoder: invalid size %d x %d\n", width, height);


        if (opened)
        {
            Log("-H264Encoder: reconfig size\n");

#ifdef X264_REOPEN
            /* Here we close and reopen the encoder to apply the new size */
            
            if (enc != NULL)
            {
                    x264_encoder_close(enc);
                    enc =NULL;
            }
            opened=false;
            this->width = width;
            this->height = height;
            numPixels = width*height;
            return OpenCodec();
#else
          /*
           *  x264_encoder_reconfig does not seem to work with size.
           * it prodices a log such as:
           *
           * "x264 Input picture width (352) is greater than stride (250)"
           */
          // Set encoding context size

            
            params.i_width 	= width;
            params.i_height	= height;
            int ret = x264_encoder_reconfig(enc,&params);
            if ( ret == 0 )
            {
                // Update size parameters
                this->width = width;
                this->height = height;
                numPixels = width*height;

                // Reset picture structures
                memset(&pic,0,sizeof(x264_picture_t));
                memset(&pic_out,0,sizeof(x264_picture_t));

                //Set picture type
                pic.i_type = X264_TYPE_AUTO;
                return 1;
            }
            else
            {
                return Error("264Encoder: Failed to resize. X264 errcode=%d.\n", ret);
            }
#endif
	}
        else
        {
            this->width = width;
            this->height = height;
            numPixels = width*height;

            return OpenCodec();
        }
}

/************************
* SetFrameRate
* 	Indica el numero de frames por segudno actual
**************************/
int H264Encoder::SetFrameRate(int frames,int kbits,int intraPeriod)
{
	// Save frame rate
	if (frames>0)
		fps=frames;
	else
		fps=10;

	// Save bitrate
	if (kbits>0)
		bitrate=kbits;

	//Save intra period
	if (intraPeriod>0)
		//Set maximium intra period
		this->intraPeriod = intraPeriod;
	else
		//No maximum intra period
		this->intraPeriod = X264_KEYINT_MAX_INFINITE;

	//Check if already opened
	if (opened)
	{
		//Reconfig parameters -> FPS is not allowed to be recondigured
		params.i_keyint_max         = intraPeriod ;
		params.i_frame_reference    = 1;
		params.rc.i_rc_method	    = X264_RC_ABR;
		params.rc.i_bitrate         = 0.8*bitrate;
		params.rc.i_vbv_max_bitrate = 1.1*bitrate;
		params.rc.i_vbv_buffer_size = 1.1*bitrate/fps;
		params.rc.f_vbv_buffer_init = 0;
		params.rc.f_rate_tolerance  = 0.2;
		params.i_fps_num	    	= fps;
		//Reconfig
		x264_encoder_reconfig(enc,&params);
	}
	
	return 1;
}

/**********************
* OpenCodec
*	Abre el codec
***********************/
int H264Encoder::OpenCodec()
{
	Log("-OpenCodec H264 [%dkbps,%dfps,%dintra]\n",bitrate,fps,intraPeriod);

	// Check 
	if (opened)
		return Error("Codec already opened\n");

	// Reset default values
	x264_param_default(&params);

	// Use a defulat preset
	x264_param_default_preset(&params,"medium","zerolatency");

	// Set log
	params.pf_log               = X264_log;

#ifdef MCUDEBUG
	//Set debug level loging
	params.i_log_level          = X264_LOG_INFO;
#else
	//Set error level for loging
	params.i_log_level          = X264_LOG_ERROR;
#endif


	// Set encoding context size
	params.i_width 	= width;
	params.i_height	= height;

	// Set parameters
	params.i_keyint_max         = intraPeriod;
	params.i_frame_reference    = 1;
	params.rc.i_rc_method	    = X264_RC_ABR;
	params.rc.i_bitrate         = 0.8*bitrate;
	params.rc.i_vbv_max_bitrate = 1.1*bitrate;
	params.rc.i_vbv_buffer_size = (1.1*bitrate)/fps;
	params.rc.f_vbv_buffer_init = 0;
	params.rc.f_rate_tolerance  = 0.2;
	params.rc.b_stat_write      = 0;
	params.i_slice_max_size     = RTPPAYLOADSIZE-8;
	params.i_threads	    = 1; //0 is auto!!
	params.b_sliced_threads	    = 0;
	params.rc.i_lookahead       = 0;
	params.i_sync_lookahead	    = 0;
	params.i_bframe             = 0;
	params.b_annexb		    = 0; 
	params.b_repeat_headers     = 1;
	params.i_fps_num	    = fps;
	params.i_fps_den	    = 1;
	params.b_intra_refresh	    = (intraRefresh) ? 1 : 0;
	params.vui.i_chroma_loc	    = 0;
	params.i_scenecut_threshold = 0;
	params.analyse.i_subpel_refine = qPel; //Subpixel motion estimation and mode decision :3 qpel (medim:6, ultrafast:1)
	Log("h264 qPel is %d.\n", qPel );
	Log("h264: progressive intra refresh is %s.\n", (intraRefresh)?"enabled" : "disabled" );
	//Get profile and level
	int profile = strtol(h264ProfileLevelId.substr(0,2).c_str(),NULL,16);
	int level   = strtol(h264ProfileLevelId.substr(4,2).c_str(),NULL,16);

	//Set level
	if (level < 9)
	{
		// level set by x264 based on bw settings
		params.i_level_idc = -1;
	}
	else if (level >13 && level < 20)
	{
		params.i_level_idc = 13;
	}
	else if (level > 22 && level < 30)
	{
		 params.i_level_idc = 30;
	}
	else if (level > 32 && level < 40)
	{
		 params.i_level_idc = 32;
	}
	else if (level > 42 && level < 50)
	{
		 params.i_level_idc = 42;
	}
	else if (level > 52)
	{
		 params.i_level_idc = 52;
	}
	else
	{
		params.i_level_idc = level;
	}

	//Depending on the profile in hex
	switch (profile)
	{
		case 100:
		case 88:
			//High
			x264_param_apply_profile(&params,"high");
			Log("h264: encoding with profile high and level %d.\n", level);
			break;
		case 77:
			//Main
			x264_param_apply_profile(&params,"main");
			Log("h264: encoding with profile main and level %d.\n", level);
			break;
		case 66:
		default:
			//Baseline
			Log("h264: encoding with profile baseline and level %d.\n", level);
			x264_param_apply_profile(&params,"baseline");
	}

	// Open encoder
	enc = x264_encoder_open(&params);

	//Check it is correct
	if (!enc)
		return Error("Could not open h264 codec\n");

	// Clean pictures
	memset(&pic,0,sizeof(x264_picture_t));
	memset(&pic_out,0,sizeof(x264_picture_t));

	//Set picture type
	pic.i_type = X264_TYPE_AUTO;
	
	// We are opened
	opened = true;

	// Exit
	return 1;
}


/**********************
* EncodeFrame
*	Codifica un frame
***********************/
VideoFrame* H264Encoder::EncodeFrame(BYTE *buffer,DWORD bufferSize)
{
	if(!opened)
	{
		Error("-Codec not opened\n");
		return NULL;
	}

	//Comprobamos el tama�o
	if (numPixels*3/2 != bufferSize)
	{
		Error("-H264 EncodeFrame length error [%d,%d]\n",numPixels*5/4,bufferSize);
		return NULL;
	}

	//POnemos los valores
	pic.img.plane[0] = buffer;
	pic.img.plane[1] = buffer+numPixels;
	pic.img.plane[2] = buffer+numPixels*5/4;
	pic.img.i_stride[0] = width;
	pic.img.i_stride[1] = width/2;
	pic.img.i_stride[2] = width/2; 
	pic.img.i_csp   = X264_CSP_I420;
	pic.img.i_plane = 3;
	pic.i_pts  = pts++;
	
	/* IVeS - patch send one I-frame every 2 sec during first intra period */
	if ( pic.i_pts < 8*fps && (pic.i_pts % (2*fps) == 0) )
    {
        pic.i_type = X264_TYPE_I;
    }
	
	
	// Encode frame and get length
	int len = x264_encoder_encode(enc, &nals, &numNals, &pic, &pic_out);

	//Check it
	if (len<=0)
	{
		//Error
		Error("Error encoding frame [len:%d]\n",len);
		return NULL;
	}
	//Check size
	if (!frame)
		//Create new frame
		frame = new VideoFrame(type,len);

	//Set the media
	frame->SetMedia(nals[0].p_payload,len);

	//Set width and height
	frame->SetWidth(width);
	frame->SetHeight(height);

	//Set intra
	frame->SetIntra(pic_out.b_keyframe);

	//Unset type
	pic.i_type = X264_TYPE_AUTO;

	//Emtpy rtp info
	frame->ClearRTPPacketizationInfo();

	//Add packetization
	for (DWORD i=0;i<numNals;i++)
	{
		// Get NAL data pointer skiping the header
		BYTE* nalData = nals[i].p_payload+4;
		//Get size
		DWORD nalSize = nals[i].i_payload-4;
		//Get nal pos
		DWORD pos = nalData - nals[0].p_payload;
		//Get nal type
		BYTE nalType = (nalData[0] & 0x1f);
		//Check if it is an SPS
		if (nalType==7)
		{
			//Get profile as hex representation
			DWORD profileLevel = strtol (h264ProfileLevelId.c_str(),NULL,16);
			//Modify profile level ID to match offered one
			set3(nalData,1,profileLevel);
		}
		//Add rtp packet
		frame->AddRtpPacket(pos,nalSize,NULL,0);
	}

	//Set first nal
	curNal = 0;
	
	return frame;
}

/**********************
* FastPictureUpdate
*	Manda un frame entero
***********************/
int H264Encoder::FastPictureUpdate()
{
	//Set next picture type
	pic.i_type = X264_TYPE_I;

	return 1;
}

