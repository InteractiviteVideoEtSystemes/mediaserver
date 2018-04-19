package org.murillo.mscontrol.mediagroup;

import java.util.HashSet;
import java.util.Set;

import javax.media.mscontrol.MsControlException;
import javax.media.mscontrol.join.Joinable;
import javax.media.mscontrol.join.JoinableStream;
import javax.media.mscontrol.join.JoinableStream.StreamType;

import org.murillo.MediaServer.Codecs;
import org.murillo.MediaServer.Codecs.MediaType;
import org.murillo.mscontrol.JoinableContainerImpl;
import org.murillo.mscontrol.JoinableStreamImpl;
import org.murillo.mscontrol.MediaSessionImpl;
import org.murillo.mscontrol.mixer.MixerAdapterJoinableStreamAudio;
import org.murillo.mscontrol.mixer.MixerAdapterJoinableStreamVideo;
import org.murillo.mscontrol.networkconnection.NetworkConnectionJoinableStream;

public class RecorderJoinableStream extends JoinableStreamImpl implements JoinableStream{

	   private final  RecorderImpl recorder;
	   private final StreamType type;
	   private final MediaType media;
	   private Set<Joinable> sending;

	

	public RecorderJoinableStream(MediaSessionImpl session, RecorderImpl recorder, StreamType type) {
	       //Call parent
        super(session, (JoinableContainerImpl) recorder.getContainer(),type);
        //Store values
        this.recorder = recorder;
        this.type = type;
        //Get mapping
        if (type.equals(StreamType.audio))
            //Audio
            media = Codecs.MediaType.AUDIO;
        else if(type.equals(StreamType.video))
            //Video
            media = Codecs.MediaType.VIDEO;
        else if(type.equals(StreamType.message))
            //Text
            media = Codecs.MediaType.TEXT;
        else
            //uuh??
            media = null;
        //Create set
        sending = new HashSet<Joinable>();
    }
	
	 public RecorderImpl getRecorder() {
	        //Return parent object
	        return recorder;
	    }

	@Override
	protected void attachEndpoint(NetworkConnectionJoinableStream networkConnectionJoinableStream)
			throws MsControlException {
		// TODO Auto-generated method stub
		
	}

	@Override
	protected void attachPlayer(PlayerJoinableStream playerJoinableStream) throws MsControlException {
		// TODO Auto-generated method stub
		
	}

	@Override
	protected void attachMixer(MixerAdapterJoinableStreamAudio mixerAdapterJoinableStreamAudio)
			throws MsControlException {
		// TODO Auto-generated method stub
		
	}

	@Override
	protected void attachMixer(MixerAdapterJoinableStreamVideo mixerAdapterJoinableStreamVideo)
			throws MsControlException {
		// TODO Auto-generated method stub
		
	}

	@Override
	protected void dettach() throws MsControlException {
		// TODO Auto-generated method stub
		
	}

	@Override
	protected void attachRecorder(RecorderJoinableStream recorderJoinableStream) throws MsControlException {
		// TODO Auto-generated method stub
		
	}
	
	


}
