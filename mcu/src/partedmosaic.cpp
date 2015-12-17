#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "log.h"
#include "partedmosaic.h"
#include <stdexcept>

/***********************
* PartedMosaic
*	Constructor
************************/
PartedMosaic::PartedMosaic(Type type, DWORD size) : Mosaic(type,size)
{
	//Divide mosaic
	switch(type)
	{
		case mosaic1x1:
			//Set num rows and cols
			mosaicCols = 1;
			mosaicRows = 1;
			break;
		case mosaic2x2:
			//Set num rows and cols
			mosaicCols = 2;
			mosaicRows = 2;
			break;
		case mosaic3x3:
			//Set num rows and cols
			mosaicCols = 3;
			mosaicRows = 3;
			break;
		case mosaic4x4:
			//Set num rows and cols
			mosaicCols = 4;
			mosaicRows = 4;
			break;
		default:
			throw new std::runtime_error("Unknown mosaic type\n");

	}
	
	mosaicWidth = (int) mosaicTotalWidth/mosaicCols;
	// if we have an odd width , we round to the smallest even width.
	if (mosaicWidth %2)
			mosaicWidth=mosaicWidth-1;
	mosaicHeight = (int) mosaicTotalHeight/mosaicRows;
	// if we have an odd height , we round to the smallest even height.
	if (mosaicHeight %2)
			mosaicHeight=mosaicHeight-1;
}

/***********************
* PartedMosaic
*	Destructor
************************/
PartedMosaic::~PartedMosaic()
{
}

/*****************************
* Update
* 	Update slot of mosaic with given image
*****************************/
int PartedMosaic::Update(int pos, BYTE *image, int imgWidth, int imgHeight)
{
	//Check size
	if (!image && !imgHeight && !imgHeight)
	{
		//Clean position
		Clean(pos);
		//Exit
		return 0;

	}

	//image = ApplyParticipantOverlay(pos, image, imgWidth, imgHeight);
	// Compute ratio
	DWORD picRatio = ComputeAspectRatio(imgWidth,imgHeight);
	
	DWORD mosaicNumPixels = mosaicTotalWidth*mosaicTotalHeight;
	DWORD offset,offset2;
	BYTE *lineaY;
	BYTE *lineaU;
	BYTE *lineaV;
	
	DWORD imgNumPixels = imgHeight*imgWidth;
	BYTE *imageY = image;
	BYTE *imageU  = image  + imgNumPixels;
	BYTE *imageV  = imageU + imgNumPixels/4;

	//Check it's in the mosaic
	if (pos < 0 || pos >= numSlots)
		return 0;

	//Get slot position in mosaic
	int i = pos / mosaicCols;
	int j = pos - i*mosaicCols;

	//Get offsets
	offset = (mosaicTotalWidth*mosaicHeight*i) + mosaicWidth*j; 
	offset2 = (mosaicTotalWidth*mosaicHeight/4)*i+(mosaicWidth/2)*j;

	//Get plane pointers
	lineaY = mosaic + offset;
	lineaU = mosaic + mosaicNumPixels + offset2;
	lineaV = lineaU + mosaicNumPixels/4; 

	//Check if the sizes are equal 
	if ((imgWidth == mosaicWidth) && (imgHeight == mosaicHeight))
	{

		//Copy Y plane
		for (int i = 0; i<imgHeight; i++) 
		{
			//Copy Y line
			memcpy(lineaY, imageY, imgWidth);

			//Go to next
			lineaY += mosaicTotalWidth;
			imageY += imgWidth;
		}

		//Copy U and V planes
		for (int i = 0; i<imgHeight/2; i++) 
		{
			//Copy U and V lines
			memcpy(lineaU, imageU, imgWidth/2);
			memcpy(lineaV, imageV, imgWidth/2);

			//Go to next
			lineaU += mosaicTotalWidth/2;
			lineaV += mosaicTotalWidth/2;

			imageU += imgWidth/2;
			imageV += imgWidth/2;
		}
		
		lineaY = mosaic + offset;
		lineaU = mosaic + mosaicNumPixels + offset2;
		lineaV = lineaU + mosaicNumPixels/4; 
		
		ApplyParticipantOverlay(pos, lineaY, lineaU, lineaV, imgWidth, imgHeight, true);
	} 
	else {
		DWORD rzWidth, rzHeight, diff;
		
		if ( (picRatio/100) == (ratio/100) || !keepAspect )
		{
		    // picture and mosaic slot have the same ratio 
		    // or we want the old behavior
		    
		    rzWidth = mosaicWidth;
		    rzHeight = mosaicHeight;
		}
		else 
		{
			rzHeight = (mosaicWidth*1000)/picRatio;
			rzWidth = (mosaicHeight*picRatio)/1000;
			
			if ( mosaicHeight > rzHeight )
			{
				 /*
				  * +-----------------+
				  * |                 |
				  * +-----------------+
				  * |                 |
				  * |     Picture     |
				  * |                 |
				  * +-----------------+
				  * |                 |
				  * +-----------------+
				  */
				rzWidth = mosaicWidth;
				diff = (mosaicHeight - rzHeight)/2;
				// Diff must be even otherwise, we get an incorrect offset when comuting lineaU
				if ( diff > 0 && (diff%2) != 0 ) diff--;
				//Get plane pointers
				lineaY = mosaic + offset + mosaicTotalWidth*diff;
				lineaU = mosaic + (mosaicNumPixels + offset2 + (mosaicTotalWidth*diff/4));
				lineaV = lineaU + (mosaicNumPixels/4); 
			}
			else if ( mosaicWidth >  rzWidth )
			{
				 /*
				  * +--+-----------+--+
				  * |  |           |  |
				  * |  | Picture   |  |
				  * |  |           |  |
				  * +--+-----------+--+
				  */
				rzHeight = mosaicHeight;
				diff = (mosaicWidth - rzWidth)/2;
				
				//Get plane pointers
				lineaY = mosaic + offset + diff;
				lineaU = mosaic + mosaicNumPixels + offset2  + diff/2;
				lineaV = lineaU + mosaicNumPixels/4;
			}
			else
			{
				lineaY = mosaic + offset;
				rzWidth = mosaicWidth;
				rzHeight = mosaicHeight;
			}
		}
		//Set resize
		resizer[pos]->SetResize(imgWidth,imgHeight,imgWidth,rzWidth,rzHeight,mosaicTotalWidth);

		//And resize
		resizer[pos]->Resize(imageY,imageU,imageV,lineaY,lineaU,lineaV);
		ApplyParticipantOverlay(pos, lineaY, lineaU, lineaV, rzWidth, rzHeight, true);
	}

	//We have changed
	SetChanged();

	return 1;
}

/*****************************
* Clean
* 	Clean slot image
*****************************/
int PartedMosaic::Clean(int pos)
{
	DWORD mosaicNumPixels = mosaicTotalWidth*mosaicTotalHeight;
	DWORD offset,offset2;
	BYTE *lineaY;
	BYTE *lineaU;
	BYTE *lineaV;

	//Check it's in the mosaic
	if (pos >= numSlots)
		return 0;

	//Get slot position in mosaic
	int i = pos / mosaicCols;
	int j = pos - i*mosaicCols;

	//Get offsets
	offset = (mosaicTotalWidth*mosaicHeight*i) + mosaicWidth*j;
	offset2 = (mosaicTotalWidth*mosaicHeight/4)*i+(mosaicWidth/2)*j;

	//Get plane pointers
	lineaY = mosaic + offset;
	lineaU = mosaic + mosaicNumPixels + offset2;
	lineaV = lineaU + mosaicNumPixels/4;

	//Copy Y plane
	for (int i = 0; i<mosaicHeight; i++)
	{
		//Copy Y line
		memset(lineaY, (BYTE)-128, mosaicWidth);
		//Go to next
		lineaY += mosaicTotalWidth;
	}

	//Copy U and V planes
	for (int i = 0; i<mosaicHeight/2; i++)
	{
		//Copy U and V lines
		memset(lineaU, (BYTE)-128, mosaicWidth/2);
		memset(lineaV, (BYTE)-128, mosaicWidth/2);

		//Go to next
		lineaU += mosaicTotalWidth/2;
		lineaV += mosaicTotalWidth/2;
	}

	//We have changed
	SetChanged();

	return 1;
}

int PartedMosaic::GetWidth(int pos)
{
	//Check it's in the mosaic
	if (pos >= numSlots)
		return 0;

	//Get widht
	return mosaicWidth;
}
int PartedMosaic::GetHeight(int pos)
{
	//Check it's in the mosaic
	if (pos >= numSlots)
		return 0;

	//Get widht
	return mosaicHeight;
}
int PartedMosaic::GetTop(int pos)
{
	//Get slot position in mosaic
	int i = pos / mosaicCols;
	int j = pos - i*mosaicCols;

	//Get offsets
	return mosaicHeight*i;
}
int PartedMosaic::GetLeft(int pos)
{
	//Get slot position in mosaic
	int i = pos / mosaicCols;
	int j = pos - i*mosaicCols;
	//Get offsets
	return mosaicWidth*j;
}
