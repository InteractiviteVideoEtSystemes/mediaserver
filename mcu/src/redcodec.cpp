#include "redcodec.h"
#include "codecs.h"

static BYTE BOMUTF8[]			= {0xEF,0xBB,0xBF};
static BYTE LOSTREPLACEMENT[]		= {0xEF,0xBF,0xBD};


RedundentCodec::RedundentCodec()
{
    lastSeq = RTPPacket::MaxExtSeqNum;
    lastTime = 0;
    idle = true;
}

bool RedundentCodec::Decode(RTPRedundantPacket * red, TextOutput * textOutput)
{
    DWORD timeStamp = red->GetTimestamp();

    //Get extended sequence number
    DWORD seq = red->GetExtSeqNum();

    //Lost packets since last one
    DWORD lost = 0;

    //If not first
    if (lastSeq!=RTPPacket::MaxExtSeqNum)
            //Calculate losts
            lost = seq-lastSeq-1;

    //Update last sequence number
    lastSeq = seq;
    //Timestamp of first packet (either receovered or not)
    DWORD ts = timeStamp;

    //Check if we have any red pacekt
    if (red->GetRedundantCount()>0)
            //Get the timestamp of first redundant packet
            ts = red->GetRedundantTimestamp(0);

    //For each lonot recoveredt packet send a mark
    for (int i=red->GetRedundantCount();i<lost;i++)
    {

            //Create frame of lost replacement
            TextFrame frame(ts,LOSTREPLACEMENT,sizeof(LOSTREPLACEMENT));
            //Y lo reproducimos
            textOutput->SendFrame(frame);
    }

    //If we have lost too many
    if (lost>red->GetRedundantCount())
            //Get what we have available only
            lost = red->GetRedundantCount();
    //Fore each recovered packet
    for (int i=red->GetRedundantCount()-lost;i<red->GetRedundantCount();i++)
    {
            //Create frame from recovered data
            TextFrame frame(red->GetRedundantTimestamp(i),red->GetRedundantPayloadData(i),red->GetRedundantPayloadSize(i));
            //Y lo reproducimos
            textOutput->SendFrame(frame);
    }

    // Now process the primary data
    TextFrame pframe(timeStamp ,red->GetPrimaryPayloadData(),red->GetPrimaryPayloadSize());
    textOutput->SendFrame(pframe);
}

RTPRedundantPacket * RedundentCodec::Encode(MediaFrame * frame, BYTE ptype)
{
    BYTE buffer[MTU];
    //Get first
    BYTE* red = buffer;
    //Init buffer length
    DWORD bufferLen = 0;
    
    //Fill with empty redundant packets
    for (int i=0;i<2-reds.size();i++)
    {
        //Empty t140 redundancy packet
        red[0] = 0x80 | ptype;
        //Randomize time
        red[1] = rand();
        red[2] = (rand() & 0x3F) << 2;
        //No size
        red[3] = 0;
        //Increase buffer
        red += 4;
        bufferLen += 4;
    }

    //Iterate to put the header blocks
    for (RedFrames::iterator it = reds.begin();it!=reds.end();++it)
    {
            //Get frame and go to next
            MediaFrame *f = *(it);
            //Calculate diff
            DWORD diff = lastTime-f->GetTimeStamp();
            /****************************************************
             *  F: 1 bit First bit in header indicates whether another header block
             *     follows.  If 1 further header blocks follow, if 0 this is the
             *      last header block.
             *
             *  block PT: 7 bits RTP payload type for this block.
             *
             *  timestamp offset:  14 bits Unsigned offset of timestamp of this block
             *      relative to timestamp given in RTP header.  The use of an unsigned
             *      offset implies that redundant data must be sent after the primary
             *      data, and is hence a time to be subtracted from the current
             *      timestamp to determine the timestamp of the data for which this
             *      block is the redundancy.
             *
             *  block length:  10 bits Length in bytes of the corresponding data
             *      block excluding header.
             ********************************/
            red[0] = 0x80 | ptype;
            red[1] = diff >> 6;
            red[2] = (diff & 0x3F) << 2;
            red[2] |= f->GetLength() >> 8;
            red[3] = f->GetLength();
            //Increase buffer
            red += 4;
            bufferLen += 4;
    }
    //Set primary encoded data and last mark
    red[0] = ptype;
    //Increase buffer
    red++;
    bufferLen++;

    //Iterate to put the redundant data
    for (RedFrames::iterator it = reds.begin();it!=reds.end();++it)
    {
            //Get frame and go to next
            MediaFrame *f = *(it);
            //Verify buffer MTU size limit
            if (bufferLen + f->GetLength() >= MTU)
            {
                return NULL;
            }
            //Copy
            memcpy(red,f->GetData(),f->GetLength());
            //Increase sizes
            red += f->GetLength();
            bufferLen += f->GetLength();
    }

    //Check if there is frame
    if (frame)
    {
            //Verify buffer MTU size limit
            if (bufferLen + frame->GetLength() >= MTU)
            {
                return NULL;
            }
            //Copy
            memcpy(red,frame->GetData(),frame->GetLength());
            //Serialize data
            bufferLen += frame->GetLength();
            //Push frame to the redundancy queue
            reds.push_back(frame->Clone() );
    } else {
            //Push new empty frame
            reds.push_back(new TextFrame(lastTime,(wchar_t*)NULL,0));
    }
    //Send the mark bit if it is first frame after idle
    bool mark = idle && frame;
    DWORD codec;
    if ( frame  == NULL )
    {
        codec = TextCodec::T140RED;
    }
    else
    {
        if (  frame->GetType() ==  MediaFrame::Video )
        {
            codec = VideoCodec::RED;
        }
        else
        {
            codec = TextCodec::T140RED;
        }
    }

    RTPRedundantPacket * packet = new RTPRedundantPacket(
            (frame != NULL) ? frame->GetType():  MediaFrame::Text , codec );
    //Set data

    packet->SetPayload(buffer, bufferLen);

    //Set timestamp
    packet->SetTimestamp(lastTime);

    //Set mark
    packet->SetMark(mark);
    //Check size of the queue
    if (reds.size()==3)
    {
            //Delete first
            delete(reds.front());
            //Dequeue
            reds.pop_front();
    }
	bool isNotNull=false;
    //Calculate timeouts
    if (frame)
    {
			isNotNull=true;
            //Not idle anymore
            idle = false;
            lastTime = frame->GetTimeStamp();
    }
    else
    {
        int nbactivefr = 0;
		
        for (RedFrames::iterator it = reds.begin();it!=reds.end();++it)
        {
            //Get frame and go to next
            MediaFrame *f = *(it);
            //If it is not empty
            if (f->GetLength())
            {
				isNotNull=true;
                if (f->GetLength() == 3)
                {
                    if (memcmp(f->GetData(), BOMUTF8, 3) == 0)
                    {
                        nbactivefr++;
                    }
                }
            }
        }
        idle = (nbactivefr == 0);
    }
	if (isNotNull)
		return packet;
    else
    {
        delete packet;
        return NULL;
    }
}

RTPRedundantPacket * RedundentCodec::EncodeBOM(BYTE ptype)
{
    TextFrame t(0, BOMUTF8, sizeof(BOMUTF8));
    return Encode(&t,  ptype);
}

bool RedundentCodec::EncoderIsIdle()
{
    return idle;
}
