#include "overlay.h"
#include "log.h"
#include "bitstream.h"
#include <stdlib.h>
#include <string.h>
#include <Magick++.h>
#include "amf.h"

extern "C" {
#include <libswscale/swscale.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/common.h>
}

using Magick::Quantum;
    
// Helper function de check the language of a text string
// Script Code - ISO 15924
enum TextCharType
{
    OVRL_TEXT_COMMON = 215,
    OVRL_TEXT_KATAKANA = 411,
    OVRL_TEXT_HIRAGANA = 410,
    OVRL_TEXT_HAN  = 500,  		// Chinese
    OVTL_TEXT_ARABIC  = 160,
    OVTL_TEXT_CYRILLIC = 220,
    OVRL_TEXT_UNKNOWN=0
};

// See http://en.wikipedia.org/wiki/Unicode_block
inline enum TextCharType GetCharType(wchar_t c)
{
    if (c >=0 && c < 0x0250)
    {
        return OVRL_TEXT_COMMON;
    }
    else if ( ( c >= 0x0600 && c < 0x0780) 
              || 
	      ( c >= 0x08A0 && c < 0x0900) )
    {
        return OVTL_TEXT_ARABIC;
    }
    else if ( c >= 0x0400 && c < 0x0530 )
    {
        return OVTL_TEXT_CYRILLIC;
    }
    else if ( c >= 0x3040 && c < 0x30A0 )
    {
        return OVRL_TEXT_HIRAGANA;
    }
    else if ( c >= 0x30A0 && c < 0x3100 )
    {
        return OVRL_TEXT_KATAKANA;
    }
    else if ( c >= 0x31F0 && c < 0x3400 )
    {
         return OVRL_TEXT_KATAKANA;
    }
    else if ( c >= 0x3400 && c < 0xA000 )
    {
        return OVRL_TEXT_HAN;
    }
    else
    {
        return OVRL_TEXT_UNKNOWN;
    }
}
    
// First implementation of Asian / non europeran language
// to do: split the string into pieces and render then with separate fonts.
static enum TextCharType InferStringType(const std::wstring & str)
{
   
    std::wstring & str2 = ( std::wstring & ) str;
    std::wstring::iterator it; 
    
    TextCharType tp = OVRL_TEXT_COMMON, tp2;
    for ( it = str2.begin(); it != str2.end(); it++ )
    {
	tp2 = GetCharType(*it);
	switch ( tp2 )
	{
	    case OVRL_TEXT_UNKNOWN:
 	    case OVRL_TEXT_COMMON:
		break;

	    case OVRL_TEXT_KATAKANA:
	    case OVRL_TEXT_HIRAGANA:
		return tp2;

	    default:
		if (tp == OVRL_TEXT_COMMON) tp = tp2;
		break;


	}
    }
    return tp;
}

Overlay & Overlay::operator =(const Overlay& o)
{
    Log("-Overlay: Copy this=%p, other=%p\n",this, &o);
    imageBuffer = NULL;
    overlayBuffer = NULL;
    contentType = o.contentType;
    this->width = o.width;
    this->height= o.height;
    content = o.content;
    Resize(width, height);
    this->scriptCode=o.scriptCode;
}

Overlay::Overlay()
{
	//Do not display
	imageBuffer = NULL;
	overlayBuffer = NULL;
	contentType = NONE;
	this->width =0;
	this->height=0;
	Resize(width, height);
	Log("-Overlay: Construct default this=%p\n",this);
	this->scriptCode=0;
}


Overlay::Overlay(DWORD width,DWORD height)
{
	//Do not display
	imageBuffer = NULL;
	overlayBuffer = NULL;
	contentType = NONE;
	this->width = width;
	this->height = height;
	Resize(width, height);
	Log("-Overlay: Construct this=%p\n",this);
	this->scriptCode=0;
}



bool Overlay::Resize(DWORD width,DWORD height)
{
    if ( width == 0 || height == 0 )
    {
        return false;
    }
    
    if ( width == this->width && height == this->height )
    {
        return true; // has not changed
    }
    
    if (imageBuffer !=NULL) free(imageBuffer);
    if (overlayBuffer !=NULL) free(overlayBuffer);
    //Store values
    this->width = width;
    this->height = height;
    //Calculate size for overlay iage with alpha
    overlaySize = width*height*5/2+FF_INPUT_BUFFER_PADDING_SIZE+32;
    //Create overlay image
    overlayBuffer = (BYTE*)malloc(overlaySize);
    //Get aligned buffer
    overlay = ALIGNTO32(overlayBuffer);
    //Calculate size for final image i.e. without alpha
    imageSize = width*height*3/2+FF_INPUT_BUFFER_PADDING_SIZE+32;
    //Create final image
    imageBuffer = (BYTE*)malloc(imageSize);
    //Get aligned buffer
    image = ALIGNTO32(imageBuffer);
    display = false;
}

Overlay::~Overlay()
{
	
	Log("-Overlay: destruct this=%p , imageBuffer %p\n",this,imageBuffer);	
	//Free memory
	 if (imageBuffer !=NULL) free(imageBuffer);
   	 if (overlayBuffer !=NULL) free(overlayBuffer);
   
}

static int ConvertToYUVA(AVFrame * imgRGBA, BYTE *yuvaData, int width, int height);

static int ConvertToYUVA(Magick::Blob & imgRGBA, BYTE *yuvaData, int width, int height)
{
    AVFrame* rgbaFrame = NULL;

    rgbaFrame = avcodec_alloc_frame();
    int numpixels = width*height;
    
    rgbaFrame->data[0] = (BYTE *) imgRGBA.data();
    
     //Set size for planes
    rgbaFrame->linesize[0] = 4*width;
    
    ConvertToYUVA(rgbaFrame, yuvaData, width, height);
    
end_convert2:
    if (rgbaFrame)
	av_free(rgbaFrame);
	
    return 1;
}

static int ConvertToYUVA(AVFrame * imgRGBA, BYTE *yuvaData, int width, int height)
{
    SwsContext *sws = NULL;
    int numpixels = width*height;
    BYTE * rgbaData;
    AVFrame* logo = NULL;
    int res;
    
    //First from to RGBA to YUVB
    sws = sws_getContext( width, height, AV_PIX_FMT_RGBA,
			  width, height, PIX_FMT_YUVA420P,
			  SWS_FAST_BILINEAR, 0, 0, 0);
    if (sws == NULL)
    {
	//Set errror
	res = Error("Couldn't alloc sws context\n");
	// Exit
	goto end_convert;
    }

    //Allocate new one
    logo = avcodec_alloc_frame();
    if (logo == NULL)
    {
	//Set errror
	res = Error("Couldn't alloc frame\n");
	//Free resources
	goto end_convert;
    }

    //Set size for planes
    logo->linesize[0] = width;
    logo->linesize[1] = width/2;
    logo->linesize[2] = width/2;
    logo->linesize[3] = width;

    //Alloc data
    logo->data[0] = yuvaData;
    logo->data[1] = logo->data[0] + numpixels;
    logo->data[2] = logo->data[1] + numpixels / 4;
    logo->data[3] = logo->data[2] + numpixels / 4;

    //Convert
    sws_scale(sws, imgRGBA->data, imgRGBA->linesize, 0, height, logo->data, logo->linesize);
    res = 1;
	
end_convert:
   if (logo)
	av_free(logo);
	
   if (sws)
	sws_freeContext(sws);

    return res;   
}

int Overlay::LoadImage(const char* filename)
{
	
	if ( filename != NULL )
	{
	    contentType = PICTURE_BITMAP;
	    content = filename;
	}
	else
	{
	    if ( contentType != PICTURE_BITMAP )
	    {
	        return Error("previous content was not picture file");
	    }
	}
	
    try
    {
	Magick::Image render(content);
	if (height == 0 || width == 0)
	    return Error("-Overlay: no slot size. Cannot render image.\n");

	render.zoom( Magick::Geometry( width, height) );
	Magick::Blob rgbablob;
	render.magick("RGBA");
	render.write(&rgbablob);
    
	int ret =ConvertToYUVA(rgbablob, overlay, width, height);
	contentType = PICTURE_BITMAP;
	display = true;
	return ret;
    }
    catch ( Magick::Exception &error ) 
    {
	contentType = NONE;
	return Error("-Overlay: failed to load picture file %s: %s.\n", content.c_str(), error.what() );
    }
    catch ( std::exception &error2 ) 
    {
	contentType = NONE;
	return Error("-Overlay: failed to load picture file %s: %s.\n", content.c_str(), error2.what() );
    }

}

int Overlay::RenderSVG(const char* svg)
{
    if (svg != NULL) 
    {
	content = svg;
	contentType = PICTURE_VECTOR;
    }
    try
    {
	Magick::Image render( Magick::Geometry(width, height) );
	Magick::Blob rgbablob( content.c_str(), content.length() );
	render.magick("RGBA");
	render.write(&rgbablob);
    
	return ConvertToYUVA(rgbablob, overlay, this->width, this->height);
    }
    catch ( Magick::Exception &error ) 
    {
	contentType = NONE;
	return Error("-Overlay: failed to load picture file %s: %s.\n", content.c_str(), error.what() );
    }
    catch ( std::exception &error2 ) 
    {
	contentType = NONE;
	return Error("-Overlay: failed to load picture file %s: %s.\n", content.c_str(), error2.what() );
    }
}

int Overlay::RenderText(const char* msg, int scriptCode)
{
//    MagickCore::SetLogEventMask("All");

 
    if (msg) content = msg;

    if (width == 0 || height == 0)
    {
	Log("-Overlay: no slot dimension. Not rendering.\n");
    }

        Magick::Blob bob;
        Magick::Image render( Magick::Geometry(width, height),
                          Magick::Color(0, 0, 0, QuantumRange) );
        Magick::Color gray(QuantumRange/4, QuantumRange/4, QuantumRange/4, QuantumRange/4);

        render.strokeColor( gray );
        render.fillColor( gray ); // Fill color
        render.strokeWidth(2);
   try
   {
        //render.draw( Magick::DrawableRectangle( 8, height - 38, width-8, height-8 ) );

	// ImageMagick++ doc is incorrect. The correct argument deffinition are
	// (x_topleft, y_toplef, x_bottomrigh, y_bottomright, radius_x, radius_y )
	
	// To do
	
	std::string text2(content);
	Magick::TypeMetric textSize;
	UTF8Parser text;	    
	text.Parse( (BYTE *) content.c_str(), content.length() );
	
	// Check if it can be printed witthout be truncated
	
	render.draw( Magick::DrawableRoundRectangle( 10, height - 38, // Center
						     width - 8,  height-8,
						     5, 5) );
        render.fillColor( "white" ); // Fill color
	if (scriptCode != 0)
		this->scriptCode=scriptCode;
	
	if (scriptCode == 0 )
		this->scriptCode = InferStringType( text.GetWString() );

	switch ( this->scriptCode )
	{
	    case OVRL_TEXT_COMMON:
		Log("-Overlay: european text. Using helvetica.\n");
		render.font("helvetica");
		render.fontPointsize(24);
		break;
		
	    case OVRL_TEXT_KATAKANA:
		Log("-Overlay: rendering Katakana. Using font Sazanami-Mincho-Regular.\n");
//		render.font("Sazanami-Mincho-Mincho-Regular");
		render.font("SazanamiMincho");
		render.fontPointsize(24);
		break;
		

	    case OVRL_TEXT_HIRAGANA:
		Log("-Overlay: rendering Hiragana. Using font SazanamiMincho.\n");
		render.font("SazanamiMincho");
		render.fontPointsize(24);
		break;
	    
	    case OVRL_TEXT_HAN:  		// Chinese
		Log("-Overlay: rendering Han. Using font ZenKaiUni.\n");
	//	render.font("AR-PL-ZenKai-Uni-Medium");
		render.font("ZenKaiUni");
		render.fontPointsize(20);
		break;
	    
	    case OVTL_TEXT_ARABIC:
	    case OVTL_TEXT_CYRILLIC:	    
	    case OVRL_TEXT_UNKNOWN:
	    default:
		contentType = NONE;
		return Error("-Overlay: character set is not supported.\n");    
        }
	
	render.depth(8);
	render.interlaceType(Magick::NoInterlace);

	render.fontTypeMetrics( text2, &textSize );
	float curwidth = textSize.textWidth();
	


	while ( curwidth >= width - 12 )
	{
	        // Text is too large to fit in, lower the font size, truncate it. Leave some space for the ...
        	render.fontPointsize(20);
		if ( text.Truncate(1) == 0)
		{
		     contentType = NONE;
		     Error("-Overlay: not enough width to render text in slot.\n");
		     return 0;
		}
		
		text.Serialize(text2);
		text2 += " ...";
		render.fontTypeMetrics( text2, &textSize );
	        curwidth = textSize.textWidth();
	}
	Log("-Overlay: will render test %s.\n", text2.c_str() );
        render.draw( Magick::DrawableText( 10, height - 14, text2 ));
        render.magick("RGBA");
        render.write(&bob);
    
	int ret = ConvertToYUVA(bob, overlay, width, height);
	contentType = TEXT;
	display = true;
	return ret;
    }
    catch ( Magick::Exception &error )
    {
        contentType = NONE;
        return Error("-Overlay: failed to render text %s: %s.\n", content.c_str(), error.what() );
    }
}


BYTE* Overlay::Display(BYTE* frameY, BYTE* frameU, BYTE* frameV, DWORD bitmapWidth, DWORD bitmapHeight, bool changeFrame)
{
		
	//check if we have overlay
	if (!display)
	{
		switch(contentType)
		{
		    case NONE:
		        return frameY;
			
		    case PICTURE_BITMAP:
			LoadImage(NULL);
			break;
			
		    case PICTURE_VECTOR:
			RenderSVG(NULL);
			break;

		    case TEXT:
		        RenderText(NULL,0);
			break;
			
		    default:
		       break;
		}
		
		// if we could not render ...
		if (!display)
			//Return the same frame
			return frameY;
		Log("-Overlay: overlay rendered in %dx%d.\n", width, height);
	}
	//Get source

	
	
	if ( bitmapWidth == 0 || bitmapHeight == 0 )
	{
		Error("-Overlay: failed to apply overlay. One of bitmap dimention is 0.\n");
		return frameY;
	}
	
	BYTE* srcY1;
	BYTE* srcY2;
	BYTE* srcU;
	BYTE* srcV;
	//Get overlay
	BYTE* ovrY1 = overlay;
	BYTE* ovrY2 = overlay+width;
	BYTE* ovrU  = overlay+width*height;
	BYTE* ovrV  = overlay+(width*height*5)/4;
	BYTE* ovrA1 = overlay+ (width*height*3)/2;
	BYTE* ovrA2 = overlay+ (width*height*3)/2+width;
	//Get destingation
	BYTE* dstY1 = image;
	BYTE* dstY2 = image+width;
	BYTE* dstU  = image+width*height;
	BYTE* dstV  = dstU + (width*height)/4;

	int step = bitmapWidth + (bitmapWidth - width);
	int step2 = (bitmapWidth - width)/2;

	
	/* check if offsets are correct and if bitmap is large enough */
	if ( width > bitmapWidth || height >  bitmapHeight) 
	{
	    Error("-Overlay: Bitmap is smaller than overlay. Cannot apply.\n");
	    return frameY;
	}
	
	for (int j=0; j<height/2; ++j)
	{
		srcY1 = frameY + 2*bitmapWidth*j;
		srcY2 = srcY1 + bitmapWidth;
		srcU = frameU + (j*bitmapWidth)/2;
		srcV = frameV + (j*bitmapWidth)/2;

		for (int i=0; i<width/2; ++i)
		{
			//Get alpha values
			BYTE a11 = *(ovrA1++);
			BYTE a12 = *(ovrA1++);
			BYTE a21 = *(ovrA2++);
			BYTE a22 = *(ovrA2++);
			//Check
			if (a11==0)
			{
				//Set alpha
				*(dstY1++) = *(srcY1++);
				//Increase pointer
				++ovrY1;
			} else if (a11==255) {
				//Set original image
				*(dstY1++) = *(ovrY1++);
				//Increase pointer
				++srcY1;
			} else {
				DWORD n = 255-a11;
				DWORD oY = *(ovrY1++);
				DWORD sY = *(srcY1++);
				*(dstY1++) = (oY*a11+sY*n)/255;
				//Calculate and move pointer
				//*(dstY1++) = (((DWORD)(*(ovrY1++)))*a11 + (((DWORD)(*(srcY1++)))*(~a11)))/255;
			}
			//Check
			if (a12==0)
			{
				//Set alpha
				*(dstY1++) = *(srcY1++);
				//Increase pointer
				++ovrY1;
			} else if (a12==255) {
				//Set original image
				*(dstY1++) = *(ovrY1++);
				//Increase pointer
				++srcY1;
			} else {
				DWORD n = 255-a12;
				DWORD oY = *(ovrY1++);
				DWORD sY = *(srcY1++);
				*(dstY1++) = (oY*a12+sY*n)/255;
				//Calculate and move pointer
				//*(dstY1++) = (((DWORD)(*(ovrY1++)))*a12 + (((DWORD)(*(srcY1++)))*(~a12)))/255;
			}
			//Check
			if (a21==0)
			{
				//Set alpha
				*(dstY2++) = *(srcY2++);
				//Increase pointer
				++ovrY2;
			} else if (a21==255) {
				//Set original image
				*(dstY2++) = *(ovrY2++);
				//Increase pointer
				++srcY2;
			} else {
				DWORD n = 255-a21;
				DWORD oY = *(ovrY2++);
				DWORD sY = *(srcY2++);
				*(dstY2++) = (oY*a21+sY*n)/255;
				//Calculate and move pointer
				//*(dstY2++) = (((DWORD)(*(ovrY2++)))*a21 + (((DWORD)(*(srcY2++)))*(~a21)))/255;
			}
			//Check
			if (a22==0)
			{
				//Set alpha
				*(dstY2++) = *(srcY2++);
				//Increase pointer
				++ovrY2;
			} else if (a22==255) {
				//Set original image
				*(dstY2++) = *(ovrY2++);
				//Increase pointer
				++srcY2;
			} else {
				DWORD n = 255-a22;
				DWORD oY = *(ovrY2++);
				DWORD sY = *(srcY2++);
				*(dstY2++) = (oY*a22+sY*n)/255;
				//Calculate and move pointer
				//*(dstY2++) = (((DWORD)(*(ovrY2++)))*a22 + (((DWORD)(*(srcY2++)))*(~a22)))/255;
			}

			//Summ all alphas
			DWORD alpha = a11+a21+a21+a22;
			//Check UV
			if (alpha==0)
			{
				//Set UV
				*(dstU++) = *(srcU++);
				*(dstV++) = *(srcV++);
				//Increase pointers
				++ovrU;
				++ovrV;
			} else if (alpha == 1020) {
				//Set UV
				*(dstU++) = *(ovrU++);
				*(dstV++) = *(ovrV++);
				//Increase pointers
				++srcU;
				++srcV;
			} else {
				DWORD negalpha = 1020-alpha;
				DWORD oU = *(ovrU++);
				DWORD oV = *(ovrV++);
				DWORD sU = *(srcU++);
				DWORD sV = *(srcV++);
				*(dstU++) = (oU*alpha+sU*negalpha)/1020;
				*(dstV++) = (oV*alpha+sV*negalpha)/1020;
				//Calculate and move pointer
				//*(dstU++) = (((DWORD)(*(ovrU++)))*alpha + (((DWORD)(*(srcU++)))*negalpha))/1020;
				//*(dstV++) = (((DWORD)(*(ovrV++)))*alpha + (((DWORD)(*(srcV++)))*negalpha))/1020;

			}
		}

		// There is a remaining pixel
		if ( width % 2 )
		{
			//Get alpha values
			BYTE a11 = *(ovrA1++);
			BYTE a12 = *(ovrA1);
			BYTE a21 = *(ovrA2++);
			BYTE a22 = *(ovrA2);
			//Check
			if (a11==0)
			{
				//Set alpha
				*(dstY1++) = *(srcY1++);
				//Increase pointer
				++ovrY1;
			} else if (a11==255) {
				//Set original image
				*(dstY1++) = *(ovrY1++);
				//Increase pointer
				++srcY1;
			} else {
				DWORD n = 255-a11;
				DWORD oY = *(ovrY1++);
				DWORD sY = *(srcY1++);
				*(dstY1++) = (oY*a11+sY*n)/255;
				//Calculate and move pointer
			}

			if (a21==0)
			{
				//Set alpha
				*(dstY2++) = *(srcY2++);
				//Increase pointer
				++ovrY2;
			} else if (a21==255) {
				//Set original image
				*(dstY2++) = *(ovrY2++);
				//Increase pointer
				++srcY2;
			} else {
				DWORD n = 255-a21;
				DWORD oY = *(ovrY2++);
				DWORD sY = *(srcY2++);
				*(dstY2++) = (oY*a21+sY*n)/255;
				//Calculate and move pointer
				//*(dstY2++) = (((DWORD)(*(ovrY2++)))*a21 + (((DWORD)(*(srcY2++)))*(~a21)))/255;
			}
		}

		//Skip Y line
		dstY1 += width;
		dstY2 += width;
		ovrY1 += width;
		ovrY2 += width;
		ovrA1 += width;
		ovrA2 += width;
	}

	//And return overlay
	if (changeFrame)
	{
		for (int j=0; j<height/2; ++j)
		{
			srcY1 = frameY + 2*bitmapWidth*j;
			srcY2 = srcY1 + bitmapWidth;
			srcU = frameU + (j*bitmapWidth)/2;
			srcV = frameV + (j*bitmapWidth)/2;

			dstY1 = image + 2*width*j;
			dstY2 = dstY1 + width;
			dstU  = image + width*height + (width*j)/2;
			dstV  = dstU + (width*height)/4; 

			memcpy( srcY1, dstY1, width );
			memcpy( srcY2, dstY2, width );
			memcpy( srcU, dstU, width/2 );
			memcpy( srcV, dstV, width/2 );
		}
		return image;
	}
	else
		return image;
}
