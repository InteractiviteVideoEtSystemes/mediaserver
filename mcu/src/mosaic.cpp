#include "log.h"
#include "mosaic.h"
#include "partedmosaic.h"
#include "asymmetricmosaic.h"
#include "pipmosaic.h"
#include <stdexcept>
#include <vector>
#include <map>
#include <string.h>
#include <stdlib.h>
extern "C" {
#include <libswscale/swscale.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/common.h>
}

int Mosaic::GetNumSlotsForType(Mosaic::Type type)
{
	//Depending on the type
	switch(type)
	{
		case mosaic1x1:
			return 1;
		case mosaic2x2:
			return 4;
		case mosaic3x3:
			return 9;
		case mosaic3p4:
			return  7;
		case mosaic1p7:
			return 8;
		case mosaic1p5:
			return  6;
		case mosaic1p1:
			return 2;
		case mosaicPIP1:
			return 2;
		case mosaicPIP3:
			return 4;
		case mosaic4x4:
			return 16;
		case mosaic1p4:
			return 16;
	}
	//Error
	return Error("-Unknown mosaic type %d\n",type);
}

Mosaic::Mosaic(Type type,DWORD size)
{
	//Get width and height
	mosaicTotalWidth = ::GetWidth(size);
	mosaicTotalHeight = ::GetHeight(size);
	ratio = ComputeAspectRatio(mosaicTotalWidth, mosaicTotalHeight);
	//Store mosaic type
	mosaicType = type;

	//Calculate total size
	mosaicSize = mosaicTotalWidth*mosaicTotalHeight*3/2+FF_INPUT_BUFFER_PADDING_SIZE+32;
	//Allocate memory
	mosaicBuffer = (BYTE *) malloc(mosaicSize);
	//Get aligned
	mosaic = ALIGNTO32(mosaicBuffer);
	//Reset mosaic
	memset(mosaicBuffer ,(BYTE)-128,mosaicSize);

	//Store number of slots
	numSlots = GetNumSlotsForType(type);

	//Allocate sizes
	mosaicSlots = (int*)malloc(numSlots*sizeof(int));
	mosaicPos   = (int*)malloc(numSlots*sizeof(int));
	mosaicSlotsBlockingTime = (QWORD*)malloc(numSlots*sizeof(QWORD));

	//Empty them
	memset(mosaicSlots,0,numSlots*sizeof(int));
	memset(mosaicPos,0,numSlots*sizeof(int));
	memset(mosaicSlotsBlockingTime,0,numSlots*sizeof(QWORD));

	//Alloc resizers
	resizer = (FrameScaler**)malloc(numSlots*sizeof(FrameScaler*));

	//Create each resize
	for (int pos=0;pos<numSlots;pos++)
		//New scaler
		resizer[pos] = new FrameScaler();

	//Not changed
	mosaicChanged = false;

	//NOt need to add overlay
	overlayNeedsUpdate = false;

	//No overlay
	overlay = false;

	//No vad particpant
	vadParticipant = 0;

	keepAspect = true;
}

Mosaic::~Mosaic()
{
	std::map<int, Overlay *>::iterator it;

	//Free memory
	free(mosaicBuffer);

	//Delete each resize
	for (int i=0;i<numSlots;i++)
		//Delete
		delete resizer[i];

	//Free memory
	free(resizer);

	//If already have slot list
	if (mosaicSlots)
		//Delete it
		free(mosaicSlots);

	//If already have position list
	if (mosaicPos)
		//Delete it
		free(mosaicPos);
	//Check blocking time
	if (mosaicSlotsBlockingTime)
		//Free it
		free(mosaicSlotsBlockingTime);
		
	
	// Clean all overlays
	for (it = overlays.begin(); it != overlays.end(); ++it)
	{
	    if (it->second != NULL) delete it->second;
	}
	
	overlays.clear();
}

/************************
* SetSlot
*	Set slot participant
*************************/
int Mosaic::SetSlot(int num,int id)
{
	//Set wihtout blocking
	return SetSlot(num,id,0);
}

DWORD Mosaic::ComputeAspectRatio(DWORD imgWidth, DWORD imgHeight)
{
	DWORD tmpratio = 1000*imgWidth/imgHeight;
	
	switch( tmpratio/10 )
	{
		case 133: // VGA
		case 122: // CIF and so on
		case 146: // SIF and so on
		    // All these definition need to be stretched to have 4:3 aspect
		    return 1333;
		    
		case 177:
		    // 16:9 aspect ratio
		    return 1777;
		    
		default:
		    // Unknown aspect ratto
		    return tmpratio;
	}
}
/************************
* SetSlot
*	Set slot participant
*************************/
int Mosaic::SetSlot(int num,int id,QWORD blockedUntil)
{
	//Check num
	Participants::iterator it;
	if (num>numSlots-1 || num<0)
		//Exit
		return Error("Slot %d not in mosaic \n", num);






	if (mosaicSlots[num] == SlotVAD && id != SlotVAD)
	{
		//Log
		Log("-SetSlot [slot=%d was vadslot.\n",num);
		vadParticipant = 0;
	}
	//Set slot to participant id
	if (id != SlotReset) mosaicSlots[num] = id;

	//If we fix a participant
	if (id>0)
	{
		//Get the id of the former participant in that slot
		int partId = mosaicPos[num];
		//Find new participant
		it = participants.find(id);
		//If it is found
		if (it!=participants.end())
		{
			//Get position
			int pos = it->second;
			//Ensure it is inside bounds and was shown
			if (pos>=0 && pos<numSlots)
			{
				//Set the old position free
				mosaicSlots[pos] = SlotFree;
				//Clean slot position
				mosaicPos[pos] = 0;
			}
			// change the output position
			it->second = num;
			//Set slot position
			mosaicPos[num] = id;
		}
		//Find old participant
		it = participants.find(partId);
		Log("-SetSlot: oldParticioant in slot %d is %d.\n", num, partId);
		//IF it was there
		if (it!=participants.end() && id != partId)
		{
			//Get next free slot for it
			it->second = GetNextFreeSlot(partId);
			Log("-SetSlot: moved old participant %d in slot %d.\n", partId, it->second);
		}
	} else if (id == SlotFree) {
		//Get the id of the participant in that slot
		int partId = mosaicPos[num];
		//Find it
		it = participants.find(partId);
		//IF it was there
		if (it!=participants.end())
			//participant is not shown, yet
			it->second = NotShown;
		//Clean slot position
		mosaicPos[num] = 0;
	} else if (id == SlotLocked) {
		//Get the id of the participant in that slot
		int partId = mosaicPos[num];
		//Find it
		it = participants.find(partId);
		//IF it was there
		if (it!=participants.end())
			//Get next free slot for it
			it->second = GetNextFreeSlot(partId);
		//Clean slot position
		mosaicPos[num] = 0;
	} else if (id == SlotVAD) {
	
		if (mosaicSlots[num] != SlotVAD)
		{
			//Get the id of the participant in that slot
			int partId = mosaicPos[num];
			//Find it
			Participants::iterator it = participants.find(partId);
			//IF it was there
			if (it!=participants.end())
				//Get next free slot for it
				it->second = GetNextFreeSlot(partId);
			//Clean slot position
			mosaicPos[num] = 0;
		}
		
	} else if (id == SlotReset) {
		if (mosaicSlots[num] == SlotFree)
		{
			int partId = mosaicPos[num];
			it = participants.find(partId);
			//IF it was there
			if (it!=participants.end())
				it->second = NotShown;
			mosaicPos[num] = 0;
		}
		else
			Log("Cannot reset slot %d. It was not a free slot but %d.\n", num,
			    mosaicSlots[num]);
	}
	

	//Set blocking time
	mosaicSlotsBlockingTime[num] = blockedUntil;

	//Evirything ok
	return 1;
}

/************************
* GetPositions
*	Get position for participant
*************************/
int* Mosaic::GetPositions()
{
	//Return them
	return mosaicPos;
}

QWORD Mosaic::GetBlockingTime(int pos)
{
	//Check if the position is fixed
	return pos>=0 && pos<numSlots ? mosaicSlotsBlockingTime[pos] : 0;
}

/************************
* GetPosition
*	Get position for participant
*************************/
int Mosaic::GetPosition(int id)
{
	//Find it
	Participants::iterator it = participants.find(id);

	//If not found
	if (it==participants.end())
		//Exit
		return NotFound;

	//Get position
	return it->second;
}

int Mosaic::HasParticipant(int id)
{
	//Find it
	Participants::iterator it = participants.find(id);

	//If not found
	if (it==participants.end())
		//Exit
		return 0;

	//We have it
	return 1;
}

int Mosaic::GetNextFreeSlot(int id)
{
	//Look in the slots
	for (int i=0;i<numSlots;i++)
	{
		//It's lock for me or it is free
		if ((id > 0 && mosaicSlots[i]==id) || (mosaicSlots[i]==0 && mosaicPos[i]==0))
		{
			//Set our position if we are a real participant
			if (id > 0) mosaicPos[i]=id;
			//Return slot
			return i;
		}
	}

	//Not slot found
	return NotShown;
}

int Mosaic::AddParticipant(int id)
{
	PartInfo info;
	//Chck if allready added
	Participants::iterator it = participants.find(id);

	//If it was
	if (it!=participants.end())
	{
		 Log ("participant id %d was already in this mosaic with pos %d.\n", id, it->second);
		//Return it
		return it->second;
	}
	
	//Not shown by default
	int pos = GetNextFreeSlot(id);

	//Set position and VAD level to 0
	participants[id] = pos;

	//Add vad info for the participant
	info.vadLevel = 0;
	info.kickable = false;
	info.eligible = false;
	
	//Set it
	partVad[id] = info;
	
	Log("-AddParticipant [id:%d,pos:%d]\n",id,pos);

	//Return it
	return pos;
}

int Mosaic::RemoveParticipant(int id)
{
	//Find it
	Participants::iterator it = participants.find(id);

	//If not found
	if (it==participants.end())
		//Exit
		return NotFound;

	//Get position
	int pos = it->second;

	//Remove it for the list
	participants.erase(it);

	//Log
	Log("-RemoveParticipant [%d,%d]\n",id,pos);

	//If  was shown
	if (pos>=0 && pos<numSlots)
	{
		//Check if it was locked
		if (mosaicSlots[pos]==id)
			//lock it
			mosaicSlots[pos] = SlotLocked;

		//Clean slot position
		mosaicPos[pos] = 0;
		//Unblock
		mosaicSlotsBlockingTime[pos] = 0;
		Clean(pos);
	}

	//Get part info
	ParticipantInfos::iterator itVad = partVad.find(id);
	//If found
	if (itVad!=partVad.end())
		//Delete it
		partVad.erase(itVad);

	std::map<int, Overlay *>::iterator ito = overlays.find(id);
	if ( ito !=  overlays.end() )
	{
	    if (ito->second = NULL) delete ito->second;
	    overlays.erase(ito);
	}
	//Recalculate positions
	CalculatePositions();

	//Return position
	return pos;
}

int Mosaic::UpdateParticipantInfo(int id, int vadLevel)
{
	//Get participant position
	int pos = GetPosition(id);

	//Check it is in the mosaic
	if (pos==NotFound)
		//Exit
		return NotFound;

	//Get info
	ParticipantInfos::iterator it = partVad.find(id);

	//Check if present
	if (it==partVad.end())
		//Exit
		return NotFound;

	//Get info
	PartInfo &info = it->second;
	
	//Set vad level
	info.vadLevel = vadLevel;

	//IF the participant is not speaking and it is not fixed
	if (vadLevel == 0 && pos > 0 && mosaicSlots[pos] == SlotFree && id != vadParticipant)
		//Kicable
		info.kickable = true;
	else
		//Not kickable
		info.kickable = false;

	//If it is speaking and not shown
	if (vadLevel > 0 && pos == NotShown && id != vadParticipant)
		//Try to add it to the mosaic
		info.eligible = true;
	else
		//Do nothing
		info.eligible = false;

	//Check if it is movable
	if ( info.eligible || info.kickable )
		//It needs re-calculation
		return 1;
	
	//It does not need it
	return 0;
}

int Mosaic::CalculatePositions()
{
	//Get number of available slots
	int numSlots = GetNumSlots();

	Participants kickables;

	Participants::iterator it;
	ParticipantInfos::iterator it2;
	int id, vadPos;

	// Pass 1 - Build kickable slot list
	for (it2 = partVad.begin(); it2 != partVad.end(); ++it2)
	{
		id = it2->first;
	 	PartInfo &info = it2->second;
		it = participants.find(id);

		if ( info.kickable && it!=participants.end() && it->second >= 0 && it->second < numSlots)
			kickables[id] = it->second;
	}

	//Pass 2 - reshuffle
	Participants::iterator itk = kickables.begin();
	vadPos = GetVADPosition();

	for (it = participants.begin(); it != participants.end(); ++it)
	{
		bool eligible=false;
		id = it->first;
		int oldslot = it->second;
		it2 = partVad.find(id);
		if (it2 != partVad.end() )
			eligible = it2->second.eligible;

		int newslot;
		if (eligible)
		{
			// This participant is eligible. Try to get a free slot first
			newslot = GetNextFreeSlot(id);
			if (newslot == NotShown)
			{
			    // If none available select a kickable slot.
			    if ( itk != kickables.end() )
			    {
			        newslot = itk->second;
				if (newslot >=0) mosaicPos[newslot] = id;
				participants[itk->first] = NotShown; // previous partiicpant is not shown anymore.
				itk++;
			    }
			    else
			    {
			        // we cannot elect this participant. Process the next one
				continue;
			    }
			}

			// participant has been elected. Update its position now.
			participants[id] = newslot;
			Log("CalculatePosition: Elected participant %d -> slot %d.\n", id, newslot);
		
		}
		else if ( id != vadParticipant )
		{
			int oldslot = it->second;
			// if participant is not to be elected then ... do nothing. It has either been
			// kicked or remain in place (and in peace)

			if ( oldslot == NotShown )
			{
				newslot = GetNextFreeSlot(id);
				participants[id] = newslot;
				if (newslot >= 0) Log("CalculatePosition: participant %d not shown -> new slot =%d\n", id, newslot);


			}
			else if ( oldslot == vadPos )
			{
				// Special case
				// This participant was the former VAD participant and its output is set to vad slot
				// move it somewhere else
				newslot = GetNextFreeSlot(id);
				participants[id] = newslot;
				Log("CalculatePosition: participant %d old vad -> new slot =%d\n", id, newslot);
			
			}
			else
			{
			    if ( oldslot >= 0 &&  mosaicSlots[oldslot] == SlotFree)
			    {
					// participant is supposed to be shown on a free (movable) slot
					if ( mosaicPos[oldslot] != id )
					{
						 // check consistency
							 newslot = GetNextFreeSlot(id);
						 participants[id] = newslot;
					}
					else
					{
						// check if mosaic needs to be compacted
						newslot = GetNextFreeSlot(0);
						if (  newslot >= 0 && oldslot > newslot )
						{
							mosaicPos[newslot] = id;
							mosaicPos[oldslot] = 0;
							participants[id] = newslot;
							Log("CalculatePosition: moving part %d to slot %d as there is a hole oldpos = %d.\n", id, newslot , oldslot);
						}
					}
			    }
			}
		}
	}
	// Dumping positions
	for (int i=0; i< numSlots; i++)
	{
		//Log("CalculatePosition: slot #%d -> %d, part = %d.\n", i, mosaicSlots[i],  mosaicPos[i] );
		if ( mosaicSlots[i] == SlotFree )
		{
		    if ( mosaicPos[i] > 0 )
		    {
			it = participants.find(mosaicPos[i]);
			if ( it != participants.end() )
			{
				// check consistency
				if ( it->second != i )
				{
					// What should we do ?
					Log("CalculatePosition: inconsistency - slot %d contains part %d, which has pos %d\n",
					    i, mosaicPos[i], it->second);
					mosaicPos[i] = 0;
				}
			}
			else
			{
				Log("CalculatePosition: inconsistency - unknown participant referenced. Resetting.\n");
				mosaicPos[i] = 0;			    }
			}
		}
	}
}

int* Mosaic::GetSlots()
{
	return mosaicSlots;
}

int Mosaic::GetNumSlots()
{
	return numSlots;
}

void Mosaic::SetSlots(int *slots,int num)
{
	//Reset slot list
	memset(mosaicSlots,0,numSlots*sizeof(int));

	//If we have old slots
	if (!slots)
		return;

	//Which was bigger?
	if (num<numSlots)
		//Copy
		memcpy(mosaicSlots,slots,num*sizeof(int));
	else
		//Copy
		memcpy(mosaicSlots,slots,numSlots*sizeof(int));
}


Mosaic* Mosaic::CreateMosaic(Type type,DWORD size)
{
	//Create mosaic depending on composition
	switch(type)
	{
		case Mosaic::mosaic1x1:
		case Mosaic::mosaic2x2:
		case Mosaic::mosaic3x3:
		case Mosaic::mosaic4x4:
			//Set mosaic
			return new PartedMosaic(type,size);
		case Mosaic::mosaic1p1:
		case Mosaic::mosaic3p4:
		case Mosaic::mosaic1p7:
		case Mosaic::mosaic1p5:
		case Mosaic::mosaic1p4:
			//Set mosaic
			return new AsymmetricMosaic(type,size);
		case mosaicPIP1:
		case mosaicPIP3:
			return new PIPMosaic(type,size);
	}
	//Exit
	throw new std::runtime_error("Unknown mosaic type\n");
}

BYTE* Mosaic::GetFrame()
{
	//Check if there is a overlay
	if (!overlay)
		//Return mosaic without change
		return mosaic;
	//Check if we need to change
	if (overlayNeedsUpdate)
	{

		BYTE * mosaicU = mosaic + (mosaicTotalWidth*mosaicTotalHeight);
		BYTE * mosaicV = mosaicU + (mosaicTotalWidth*mosaicTotalHeight)/4;
		//Calculate and return
		overlay->Resize( mosaicTotalWidth, mosaicTotalHeight );
		return overlay->Display(mosaic, mosaicU, mosaicV, mosaicTotalWidth, mosaicTotalHeight, false);
        }
	//Return overlay
	return overlay->GetOverlay();
}

int Mosaic::SetOverlayImage(int id,const char* filename)
{
    if ( id <= 0)
    {    
	Log("-SetOverlay [%s] for mosaic\n",filename);

	//Reset any previous one
	if (overlay)
		//Delete it
		delete(overlay);
	//Create new one
	overlay = new Overlay(mosaicTotalWidth,mosaicTotalHeight);
	//And load it
	if(!overlay->LoadImage(filename))
		//Error
		return Error("Error loading picture image");
	//Display it
	overlayNeedsUpdate = true;
	return 1;
    }
    else
    {
	int ret = 0;
        Log("-SetOverlay [%s] for participant %d\n",filename, id);
	Participants::const_iterator it = participants.find(id);
	if (it != participants.end() )
	{
	    //Par is in the mosaic
	    Overlay * o = NULL;
	    std::map<int, Overlay *>::iterator ito = overlays.find(id);
		
	    // Create overlay if neede
	    if ( ito ==  overlays.end() )
	    {
		o = NULL;
	    }
	    else
	    {
		o = ito->second;
	    }
	    
	    if ( o == NULL ) o = new Overlay();
		
	    if (filename != NULL && strlen(filename) > 0)
		ret = o->LoadImage(filename);
	    else
	    {
		o->Clear();
		ret = 1;
	    }

	    if ( ito ==  overlays.end() )
	    {
		overlays[id] = o;
	    }
	}
	else
	{
	     Log("-SetOverlay [%s]: participant not found.\n", filename);
	}
	return ret;
    }
}

int Mosaic::SetOverlaySVG(int id, const char* svg)
{
	//Nothing yet
	return false;
}
int Mosaic::ResetOverlay(int id)
{
    if ( id <= 0 )
    {
	//Log
	Log("-Reset mosaic overlay\n");
	//Reset any previous one
	if (overlay)
		//Delete it
		delete(overlay);
	//remove it
	overlay = NULL;
	//OK
	return 1;
    }
    else
    {
        Log("-Reset Overlay for participant %d\n", id);
	Participants::const_iterator it = participants.find(id);
	if (it != participants.end() )
	{
	    std::map<int, Overlay *>::iterator ito = overlays.find(id);

	    if ( ito != overlays.end() )
	    {
		ito->second->Clear();
		return 1;
	    }
	}
	Log("-Reset Overlay: participant not found.\n");
	return 0;
    }
}

int Mosaic::SetOverlayTXT(int id, const char* msg,int scriptCode)
{
    int res=0; 
    Participants::const_iterator it = participants.find(id);
    if (it != participants.end() )
    {
        //Par is in the mosaic
	Overlay * o = NULL;
	std::map<int, Overlay *>::iterator ito = overlays.find(id);
	// Create overlay if neede
	if ( ito ==  overlays.end() )
	{
	   o = new Overlay();
	}
	else
	{
	    o = ito->second;
	}
	
        if (it->second >= 0 && it->second < numSlots )
	{
	    o->Resize( this->GetWidth(it->second), this->GetHeight(it->second));
	}
	if (msg != NULL)
		res =	o->RenderText(msg,scriptCode);
	    else
	        res =   o->Clear();

	if ( ito ==  overlays.end() )
	{
	   overlays[id] = o;
	}
   }
   return res;
}

int Mosaic::GetVADPosition()
{
	//for each slot
	for (int i=0;i<numSlots;i++)
		//Check slot
		if (mosaicSlots[i]==SlotVAD)
			//This is it
			return i;
	//Not found
	return NotFound;
}
int Mosaic::GetVADParticipant()
{
	return vadParticipant;
}

int Mosaic::SetVADParticipant(int id,QWORD blockedUntil)
{
	//Set it
	vadParticipant = id;
	
	//Find vad slot
	int pos = GetVADPosition();
	//If found
	if (pos>=0 && pos<numSlots)
	{
		//Set block time
		mosaicSlotsBlockingTime[pos] = blockedUntil;
		mosaicPos[pos] = id;
	}
	else
	{
		Log("-SetVADParticipant : there is no VAD slot defined in this mosaic.\n");
	}

	//Get vad info
	ParticipantInfos::iterator it = partVad.find(id);

	//If found
	if (it!=partVad.end())
	{
		//Get info
		PartInfo &info = it->second;
		//Participant is not kickable and not elegible
		info.kickable = false;
		info.eligible = false;
	}

	//Return vad position
	return pos;
}

bool Mosaic::IsFixed(DWORD pos)
{
	//Check if the position is fixed
	return pos>=0 && pos<numSlots ? mosaicSlots[pos]>0 : false;
}

int Mosaic::DrawVUMeter(int pos,DWORD val,DWORD size)
{
	//Get dimensions for slot
	DWORD width = GetWidth(pos);
	DWORD height = GetHeight(pos);
	//Get mosaic dimension
	DWORD totalWidth = GetWidth();
	DWORD totalHeight = GetHeight();
	//Get initial pos
	DWORD top = GetTop(pos);
	DWORD left = GetLeft(pos);
	//Calculate total pixels
	DWORD numPixels = totalWidth*totalHeight;
	//Get data planes
	BYTE *y = mosaic;
	BYTE *u = y+numPixels;
	BYTE *v = u+numPixels/4;

	//Get init of xVU meter
	int i = (left+9) & 0xFFFFFFF8;
	int j = (top+height-10) & 0xFFFFFFFE;
	//Set dimensions
	int w = (width-16) & 0xFFFFFFF0;
	int m = ((w-4)*val)/size & 0xFFFFFFFC;

	//Write top border
	for (int k=0;k<1;k++,j+=2)
	{
		memset(y+j*totalWidth+i			,0,w);
		memset(y+(j+1)*totalWidth+i		,0,w);
		memset(u+(j*totalWidth)/4+i/2		,-64,w/2);
		memset(v+(j*totalWidth)/4+i/2		,-64,w/2);
	}

	//Write VU
	for (int k=0;k<2;k++,j+=2)
	{
		//Left border
		memset(y+j*totalWidth+i			,0,2);
		memset(y+(j+1)*totalWidth+i		,0,2);
		memset(u+(j*totalWidth)/4+i/2		,-64,1);
		memset(v+(j*totalWidth)/4+i/2		,-64,1);
		//VU
		memset(y+j*totalWidth+i+2		,160,m);
		memset(y+(j+1)*totalWidth+i+2		,160,m);
		memset(u+(j*totalWidth)/4+i/2+2		,160,m/2);
		memset(v+(j*totalWidth)/4+i/2+2		,160,m/2);
		//VU
		memset(y+j*totalWidth+i+m+2		,0,w-m-2);
		memset(y+(j+1)*totalWidth+i+m+2		,0,w-m-2);
		memset(u+(j*totalWidth)/4+(m+i+2)/2	,-64,(w-m)/2-1);
		memset(v+(j*totalWidth)/4+(m+i+2)/2	,-64,(w-m)/2-1);
	}

	//Write bottom border
	for (int k=0;k<1;k++,j+=2)
	{
		memset(y+j*totalWidth+i			,0,w);
		memset(y+(j+1)*totalWidth+i		,0,w);
		memset(u+(j*totalWidth)/4+i/2		,-64,w/2);
		memset(v+(j*totalWidth)/4+i/2		,-64,w/2);
	}

	return 1;
}

void Mosaic::Dump()
{
	char p[16];
	char line1[1024];
	char line2[1024];

	//Empty
	*line1=0;
	*line2=0;

	for (int i=0;i<numSlots;++i)
	{
		if (i)
		{
			strcat(line1,",");
			strcat(line2,",");
		}
		sprintf(p,"%.4d",mosaicSlots[i]);
		strcat(line1,p);
		sprintf(p,"%.4d",mosaicPos[i]);
		strcat(line2,p);
	}

	Log("-MosaicSlots [%s]\n",line1);
	Log("-MosaicPos   [%s]\n",line2);

}

BYTE * Mosaic::ApplyParticipantOverlay(int pos, BYTE *picY, BYTE *picU, BYTE *picV, int imgWidth, int imgHeight, bool changeFrame)
{
	std::map<int, Overlay *>::iterator it = overlays.find(mosaicPos[pos]);

	
	// if (it != overlays.end() )
	//	Log("-ApplyOverlay: pos=%d, part=%d, hascontent=%d.\n",
	//	    pos, mosaicPos[pos], (int)  it->second->HasContent() );
	
	//If found
	if (it != overlays.end() && it->second != NULL && it->second->HasContent() )
	{
	     it->second->Resize(imgWidth, imgHeight);
	     return it->second->Display(picY, picU, picV, mosaicTotalWidth, mosaicTotalHeight, changeFrame);
	}
	return picY;
}

void Mosaic::MoveOverlays(Mosaic *other)
{
    std::map<int, Overlay *>::iterator it;

    overlays.clear();
    for (it = other->overlays.begin(); it != other->overlays.end(); ++it)
    {
	overlays[it->first] = it->second;
	Log("-MoveOverlay: moved overlay for part %d.\n", it->first);
    }	
    other->overlays.clear();

    if (other->overlay)
    {
	this->overlay = other->overlay;
	other->overlay = NULL;
    }
}

