/* 
 * File:   Endpoint.h
 * Author: Sergio
 *
 * Created on 7 de septiembre de 2011, 0:59
 */

#ifndef ENDPOINT_H
#define	ENDPOINT_H
#include "Joinable.h"
#include "websockets.h"
#include "RTPMultiplexer.h"
#include "remoterateestimator.h"

class RTPEndpoint;

class Endpoint
{
public:
	class Port
		: public RTPMultiplexer
	{
	public:
	    virtual ~Port();
	    MediaFrame::Type GetMedia() { return type; }
	    int Detach();
	    int Attach(Joinable * join);
		int SwitchJoin(Port *oldPort);
		
		int GetLocalMediaPort();
		char* GetLocalMediaHost();
		
	    MediaFrame::MediaProtocol GetTransport() const { return proto; }
	    
	    virtual int Init() = 0;
	    virtual int End() = 0;
	    
	    // Overriden if the port needs a thread to run
	    virtual int StartReceiving() 
	    { 
			receiving = true;
	        return 1; 
	    }
	    
	    virtual int StopReceiving()
	    { 
	        receiving = false;
	        return 1;
	    }
	    
	    virtual int StartSending()
	    { 
			sending = true;
	        return 1;
	    }
	    
	    virtual int StopSending()
	    {
	        sending = false;
			return 1;
	    }

	protected:
	    Joinable *joined;
	    MediaFrame::Type type;
		MediaFrame::MediaProtocol proto;
	    bool sending;
	    bool receiving;
	    bool portinited;
	    MediaStatistics stats;
	    // Protected constructir
	    Port( MediaFrame::Type type, MediaFrame::MediaProtocol transp) : RTPMultiplexer()
	    {
	        joined = NULL;
			this->type = type;
			this->proto = transp;
			sending = false;
			receiving = false;
			portinited = false;
	    }
	};

    typedef std::map<std::string,MediaStatistics> Statistics;

	Endpoint(std::wstring name,bool audioSupported,bool videoSupported,bool textSupport);
	~Endpoint();
	
	//Methods
	int Init();
	int End();
	
	//Endpoint  functionality
	int StartSending(MediaFrame::Type media,char *sendIp,int sendPort,RTPMap& rtpMap, MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	int StopSending(MediaFrame::Type media, MediaFrame::MediaRole role =  MediaFrame::VIDEO_MAIN);
	int StartReceiving(MediaFrame::Type media,RTPMap& rtpMap, MediaFrame::MediaRole role =  MediaFrame::VIDEO_MAIN);
	int StopReceiving(MediaFrame::Type media, MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	int RequestUpdate(MediaFrame::Type media, MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);

	int SetLocalCryptoSDES(MediaFrame::Type media,const char* suite, const char* key64, MediaFrame::MediaRole role =  MediaFrame::VIDEO_MAIN);
	int SetRemoteCryptoSDES(MediaFrame::Type media,const char* suite, const char* key64, MediaFrame::MediaRole role =  MediaFrame::VIDEO_MAIN);
	int SetRemoteCryptoDTLS(MediaFrame::Type media,const char *setup,const char *hash,const char *fingerprint, MediaFrame::MediaRole role =  MediaFrame::VIDEO_MAIN);
	int SetLocalSTUNCredentials(MediaFrame::Type media,const char* username, const char* pwd, MediaFrame::MediaRole role =  MediaFrame::VIDEO_MAIN);
	int SetRemoteSTUNCredentials(MediaFrame::Type media,const char* username, const char* pwd, MediaFrame::MediaRole role =  MediaFrame::VIDEO_MAIN);
	int SetRTPProperties(MediaFrame::Type media,const Properties& properties, MediaFrame::MediaRole role =  MediaFrame::VIDEO_MAIN);
	int SetRTPTsTransparency(MediaFrame::Type media, bool transparency, MediaFrame::MediaRole role =  MediaFrame::VIDEO_MAIN);

	//Attach
	int Attach(MediaFrame::Type media, MediaFrame::MediaRole role, Joinable *join);
	int Detach(MediaFrame::Type media, MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	Joinable* GetJoinable(MediaFrame::Type media, MediaFrame::MediaRole role  = MediaFrame::VIDEO_MAIN);

	std::wstring& GetName() { return name; }
	
	
	char* GetMediaCandidates( MediaFrame::MediaProtocol protocol ,MediaFrame::Type media = MediaFrame::Audio ) ;
	
	// Use other media protocol than RTP
	
	int onNewMediaConnection(MediaFrame::Type media, MediaFrame::MediaRole role,
	                         MediaFrame::MediaProtocol transp, WebSocket * ws );
	
	int ConfigureMediaConnection( MediaFrame::Type media, MediaFrame::MediaRole role, 
				      MediaFrame::MediaProtocol proto, const char * expectedPayload );

	int SetEventContextId( MediaFrame::Type media, MediaFrame::MediaRole role, int ctxId );
    int SetEventHandler( MediaFrame::Type media, MediaFrame::MediaRole role, int sessionId,	JSR309Manager* jsrManager);
	
	const Statistics * GetStatistics();

private:
	inline Port* GetPort(MediaFrame::Type media)
	{
		if ( media >= MediaFrame::Audio && media <= MediaFrame::Text )
		{
			return ports[media];
		}
		else
		{
			return NULL;
		}
	}
	
	inline Port* GetPort(MediaFrame::Type media, MediaFrame::MediaRole role)
	{
		if ( role == MediaFrame::VIDEO_MAIN )
		{
			return GetPort(media);
		}
		else if ( media == MediaFrame::Video && role != MediaFrame::VIDEO_MAIN )
		{
			return ports2[MediaFrame::Video];
		}
		else
		{
			return NULL;
		}
	}

	RTPEndpoint* GetRTPEndpoint(MediaFrame::Type media, MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	
private:
	std::wstring name;
	//RTP sessions
	Port * ports[4];
	Port * ports2[4];
	
	RemoteRateEstimator estimator;
	RemoteRateEstimator estimator2;
	EvenSource eventSource;
    Statistics stats;
};

#endif	/* ENDPOINT_H */

