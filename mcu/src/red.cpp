#include "red.h"

RTPRedundantPayload::RTPRedundantPayload(BYTE *data,DWORD size)
{
	//NO primary data yet
	primaryCodec = 0;
	primaryType = 0;
	primaryData = NULL;
	primarySize = 0;

	if ( data != NULL && size > 0 ) ParseRed(data, size);
}

void RTPRedundantPayload::ParseRed(BYTE *data,DWORD size)
{
	//Number of bytes to skip of text until primary data
	WORD skip = 0;

	//The the payload
	BYTE *payload = data;

	//redundant counter
	WORD i = 0;

	//Check if it is the last
	bool last = !(payload[i]>>7);

	//Read redundant headers
	while(!last)
	{
		//Check it
		/*
		    0                   1                    2                   3
		    0 1 2 3 4 5 6 7 8 9 0 1 2 3  4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
		   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		   |F|   block PT  |  timestamp offset         |   block length    |
		   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		   F: 1 bit First bit in header indicates whether another header block
		       follows.  If 1 further header blocks follow, if 0 this is the
		       last header block.

		   block PT: 7 bits RTP payload type for this block.

		   timestamp offset:  14 bits Unsigned offset of timestamp of this block
		       relative to timestamp given in RTP header.  The use of an unsigned
		       offset implies that redundant payload must be sent after the primary
		       payload, and is hence a time to be subtracted from the current
		       timestamp to determine the timestamp of the payload for which this
		       block is the redundancy.

		   block length:  10 bits Length in bytes of the corresponding payload
		       block excluding header.

		 */

		//Get Type
		BYTE type = payload[i++] & 0x7F;
		//Get offset
		WORD offset = payload[i++];
		offset = offset <<6 | payload[i]>>2;
		//Get size
		WORD size = payload[i++] & 0x03;
		size = size <<6 | payload[i++];
		//Append new red header
		headers.push_back(RedHeader(type,offset,skip,size));
		//Skip the redundant payload
		skip += size;
		//Check if it is the last
		last = !(payload[i]>>7);
	}
	//Get primaty type
	primaryType = payload[i] & 0x7F;
	//Skip it
	i++;
	//Get redundant payload
	redundantData = payload+i;
	//Get prymary payload
	primaryData = redundantData+skip;
	//Get size of primary payload
	primarySize = size-i-skip;
}

