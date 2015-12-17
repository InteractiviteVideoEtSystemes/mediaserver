/* 
 * File:   overlay.h
 * Author: Sergio
 *
 * Created on 29 de marzo de 2012, 23:17
 */

#ifndef OVERLAY_H
#define	OVERLAY_H
#include "config.h"
#include <string>

class Overlay
{
public:
	Overlay(DWORD width,DWORD height);
	Overlay();
	~Overlay();
	Overlay & operator =(const Overlay& o);
	int LoadImage(const char*);
	int RenderSVG(const char*);
	int RenderText(const char*,int scriptCode);
	int Clear() { contentType = NONE; }
	bool HasContent() { return contentType != NONE; }
	
	/**
	 * Resize overlay to the size of the mosaic picture (the mosaic slot)
	 *
	 * @param width size of the overlay. It MUST be exactly the same size as the mosaic slot width
	 * @param height height of the overlay. It MUST be exactly the same size as the mosaic slot height
	 * @return true if the overlay could be resized, false otherwise.
	 *
	 **/
	bool  Resize(DWORD width,DWORD height);
	
	/**
	 * Apply an overlay in a larger bitmap (a mosaic) coded in YUV.
	 *
	 * @param frameY pointer on the original picture Y plane on which the overlay will be applied. This picture may be larger
	 * than the overlay size,
	 * @param frameU pointer on U plane of the picutre
	 * @param frameV
	 * @param offset offset to apply to find the overlay position in the Y plane of the bitmap
	 *
	 * @param bitmapWidth width of the bitmap.
	 * @param bitmapHeight height of the bitmap.
	 * @param if true, the overlay is applied on the original picture (the original bitmap is changed)
	 * if false, the original bitmap is left unchanged.
	 *
	 * @return the portion of the picture on which the overlay has been applied. This is a bitmap
	 * coded in YUV that is of the size of the overlay (NOT of the size of the original bitmap).
	 * memory buffer is managed by the overlay so caller MUST NOT free the memory after usage.
	 *
	 **/
	BYTE* Display(BYTE* frameY, BYTE* frameU, BYTE* frameV, DWORD bitmapWidth, DWORD bitmapHeight, bool changeFrame = false);
	BYTE* GetOverlay() { return overlay; }
	

private:
	BYTE* overlayBuffer;
	DWORD overlaySize;
	BYTE* overlay;
	BYTE* imageBuffer;
	DWORD imageSize;
	BYTE* image;

	DWORD width;
	//DWORD bitmapWidth;
	DWORD height;
	bool display;
	std::string content;
	enum ContentType
	{
	    NONE,
	    PICTURE_BITMAP,
	    PICTURE_VECTOR,
	    TEXT
	}
	contentType;

	int scriptCode;
	
};

#endif	/* OVERLAY_H */

