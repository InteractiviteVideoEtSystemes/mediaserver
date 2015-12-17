/* 
 * File:   redcodec.h
 * Author: ebuu
 *
 * Created on 31 juillet 2014, 14:53
 */

#ifndef REDCODEC_H
#define	REDCODEC_H

#include "rtp.h"
#include "text.h"
#include <deque>

class RedundentCodec
{
public:
    RedundentCodec();

    /**
     * Decode one redundant RTP packet and send the resut into a Text output.
     * The content of the packet is assumed to be T14O with rendundancy
     * 
     * @param red redundant packet to decode
     * @param output text output which will receive the result
     * @return true if the packet was correclt decoded
     */
    
    bool Decode(RTPRedundantPacket * red, TextOutput * output);
    /**
     *
     * @param frame media frame to encode
     * @param ptype primary type ( payload type of data to encode)
     * @return
     */
    RTPRedundantPacket * Encode( MediaFrame * frame, BYTE ptype);
    RTPRedundantPacket * EncodeBOM(BYTE ptype);

    inline RTPRedundantPacket * EncodeNull( BYTE ptype)
    {
        return Encode( NULL, ptype);
    }

    bool EncoderIsIdle();
private:
    typedef std::deque<MediaFrame*> RedFrames;

    /**
     * Last req number of received pacckets
     */
    DWORD	lastSeq;

    /**
     * Previous frames for redundency
     */
    RedFrames	reds;

    /**
     * Last time a packet is encoded
     */
    DWORD       lastTime;

    /**
     * If we were idle before
     */
    bool        idle;
};

#endif	/* REDCODEC_H */

