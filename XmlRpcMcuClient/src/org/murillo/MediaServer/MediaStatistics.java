/*
 * To change this template, choose Tools | Templates
 * and open the template in the editor.
 */

package org.murillo.MediaServer;

import java.util.HashMap;
/**
 *
 * @author ebuu
 */
public class MediaStatistics
{
    public boolean  isSending = false;
    public boolean  isReceiving = false;
    public Integer  lostRecvPackets = 0;
    public Integer  numRecvPackets = 0;
    public Integer  numSendPackets = 0;
    public Integer  totalRecvBytes = 0;
    public Integer  totalSendBytes = 0;
    public Integer  sendingCodec = 0;
    public Integer  receivingCodec = 0;

    public MediaStatistics()
    {
        
    }

    public MediaStatistics(Object[] arr)
    {
        int i = 1;
        if ( arr != null )
        {
            if ( i < arr.length )
                isReceiving      = ((Integer) arr[i++])==1;
            if ( i < arr.length )
                isSending        = ((Integer)arr[i++])==1;

            if ( i < arr.length )
                lostRecvPackets  = (Integer)arr[i++];

            if ( i < arr.length )
                numRecvPackets   = (Integer)arr[i++];

            if ( i < arr.length )
                numSendPackets   = (Integer) arr[i++];

            if ( i < arr.length )
                totalRecvBytes   = (Integer) arr[i++];

            if ( i < arr.length )
                totalSendBytes   = (Integer) arr[i++];
        }
    }

    static HashMap<String,MediaStatistics> parseResult( Object[] returnVal )
    {
           //Create map
        HashMap<String,MediaStatistics> statsarr = new HashMap<String, MediaStatistics>();
        //For each value in array
        for (int i=0;i<returnVal.length;i++)
        {
            //Get array
            Object[] arr = (Object[]) returnVal[i];
            //Get media
            String media = (String)arr[0];
            //Create stats
            MediaStatistics stats = new MediaStatistics(arr);
            statsarr.put(media, stats);
        }

        return statsarr;
    }
}
