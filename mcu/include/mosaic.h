#ifndef _MOSAIC_H_
#define _MOSAIC_H_
#include "config.h"
#include "framescaler.h"
#include "overlay.h"
#include "vad.h"
#include <map>

class Mosaic
{
public:
	static const int NotShown = -1;
	static const int NotFound = -2;

	static const int SlotFree     = 0;
	static const int SlotLocked   = -1;
	static const int SlotVAD      = -2;
	static const int SlotReset    = -3; // For internal use only
	
	typedef enum
	{
		mosaic1x1	= 0,
		mosaic2x2	= 1,
		mosaic3x3	= 2,
		mosaic3p4	= 3,
		mosaic1p7	= 4,
		mosaic1p5	= 5,
		mosaic1p1	= 6,
		mosaicPIP1	= 7,
		mosaicPIP3	= 8,
		mosaic4x4	= 9,
		mosaic1p4	= 10,
	} Type;

	class PartInfo
	{
	    public:
		PartInfo() 
		{
		    vadLevel = 0;
		    kickable = false;
		    eligible = false;
		}
		int vadLevel;
		bool kickable;
		bool eligible;
	};

public:
	Mosaic(Type type,DWORD size);
	virtual ~Mosaic();

	int GetWidth()		{ return mosaicTotalWidth;}
	int GetHeight()		{ return mosaicTotalHeight;}
	int HasChanged()	{ return mosaicChanged; }
	void Reset()		{ mosaicChanged = true; }

	BYTE* GetFrame();
	virtual int Update(int index,BYTE *frame,int width,int heigth) = 0;
	virtual int Clean(int index) = 0;

	int AddParticipant(int id);
	int HasParticipant(int id);
	int RemoveParticipant(int id);
	int SetSlot(int num,int id);
	int SetSlot(int num,int id,QWORD blockedUntil);
	QWORD GetBlockingTime(int num);

	int GetPosition(int id);
	int GetVADPosition();
	int* GetPositions();
	int* GetSlots();
	int GetNumSlots();
	void SetSlots(int *slots,int num);
	bool IsFixed(DWORD pos);

	void Dump();

	int GetVADParticipant();
	int SetVADParticipant(int id,QWORD blockedUntil);

	static int GetNumSlotsForType(Type type);
	static Mosaic* CreateMosaic(Type type,DWORD size);

	int SetOverlayImage(int id, const char* filename);
	int SetOverlaySVG(int id, const char* svg);
	int SetOverlayTXT(int id, const char *msg,int scriptCode);
	int ResetOverlay(int id);
	int DrawVUMeter(int pos,DWORD val,DWORD size);

	int UpdateParticipantInfo(int id, int vadLevel);
	int CalculatePositions();
	void KeepAspectRatio(bool keep) { keepAspect = keep; }
	
	/** Compute aspect ratio of a picture and take into acconnt
	 *  the case of non square pixel in CIF / SIF formats
	 *  @param imgWidth width of picture
	 *  @param imgHeight height of picture
	 *  @return aspect ratio of picture
	 **/
	DWORD ComputeAspectRatio(DWORD imgWidth, DWORD imgHeight);
	
        /**
         * move all the participants overlays *nd the mosaic overlay from other
	 * to the current mosaic. After this call overlays of the "other" mosaic
	 * are attached to "this" mosaic.
	 *
         * @param source mosaic.
         */
	void MoveOverlays(Mosaic *other);

	/**
	 * Apply the participant overlay to inside the mosaic
	 *
	 * @param pos position of the participant inside the mosaic.
	 * @param pic base pointer of the mosaic
	 * @param offsetY offset in bytes to add to place the overlay inside the Y plane
	 * @param offsetUV offset in bytes to add to place the overlay inside the U and V planes
	 * @param imgWidth width of the participant slot.
	 * @param imgHeight height of the participant slot
	 * @param changeFrame whether or not the overlay should be directly applied to the mosaic's bitmap.
	 *
	 * @return a copy of the slot with the particpant picture combined with the overlay.
	 * this bitmap has teh size of a participant slot and is coded in YUV. Memory buffer
	 * is managed by the overlay instance and is reused every call of this method.
	 */
	BYTE *ApplyParticipantOverlay(int pos, BYTE *picY, BYTE *picU, BYTE *picV,
				      int imgWidth, int imgHeight, bool changeFrame = false);
	
protected:
	virtual int GetWidth(int pos) = 0;
	virtual int GetHeight(int pos) = 0;
	virtual int GetTop(int pos) = 0;
	virtual int GetLeft(int pos) = 0;
protected:
	void SetChanged()	{ mosaicChanged = true; overlayNeedsUpdate = true; }

protected:
	typedef std::map<int,int> Participants;
	typedef std::map<int,PartInfo> ParticipantInfos;

protected:
	Participants participants;
	ParticipantInfos partVad;
	std::map<int,Overlay *> overlays;
	int mosaicChanged;

	// information on whether slot is locked, free, fixed (= id of participant), vad
	int *mosaicSlots;

	// association between position and ids
	int *mosaicPos;
	QWORD *mosaicSlotsBlockingTime;
	QWORD *oldTimes;
	int numSlots;
	int vadParticipant;

	FrameScaler** resizer;
	BYTE* 	mosaic;
	BYTE*	mosaicBuffer;
	int 	mosaicTotalWidth;
	int 	mosaicTotalHeight;
	Type	mosaicType;
	int     mosaicSize;

	Overlay* overlay;
	bool	 overlayNeedsUpdate;

	bool  keepAspect;
	DWORD ratio; /* ratio * 1000 */

protected:
	int GetNextFreeSlot(int id);
};

#endif
