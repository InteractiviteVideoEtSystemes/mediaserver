/* 
 * File:   MediaSession.h
 * Author: Sergio
 *
 * Created on 6 de septiembre de 2011, 15:55
 */

#ifndef MEDIASESSION_H
#define	MEDIASESSION_H

#include "config.h"
#include "media.h"
#include "codecs.h"
#include "video.h"
#include "Player.h"
#include "Recorder.h"
#include "Endpoint.h"
#include "AudioMixerResource.h"
#include "VideoMixerResource.h"
#include "VideoTranscoder.h"
#include "AudioTranscoder.h"
#include <string.h>
#include "JSR309Event.h"

class MediaSession : public Player::Listener
{
public:
	class Listener
	{
	public:
		//Virtual desctructor
		virtual ~Listener(){};
	public:
		//Interface
		//virtual void onPlayerEndOfFile(MediaSession *sess, Player* player,int playerId, void *param) = 0;
		//virtual void onSipInfoFIRRequested(int EndpointId,void *param) = 0;
	};
public:
	MediaSession(std::wstring tag);
	~MediaSession();

	void SetListener(MediaSession::Listener *listener,void* param);

	int Init();
	int End();

	//Player management
	int PlayerCreate(std::wstring tag);
	int PlayerDelete(int playerId);
	//Player functionality
	int PlayerOpen(int playerId,const char* filename);
	int PlayerPlay(int playerId);
	int PlayerSeek(int playerId,QWORD time);
	int PlayerStop(int playerId);
	int PlayerClose(int playerId);

	//Recorder management
	int RecorderCreate(std::wstring tag);
	int RecorderDelete(int recorderId);
	//Recorder functionality
	int RecorderRecord(int recorderId,const char* filename);
	int RecorderStop(int recorderId);

	//Join other objects
	int RecorderAttachToEndpoint(int recorderId,int endpointId,MediaFrame::Type media);
	int RecorderAttachToAudioMixerPort(int recorderId,int mixerId,int portId);
	int RecorderAttachToVideoMixerPort(int recorderId,int mixerId,int portId);
	int RecorderDettach(int recorderId,MediaFrame::Type media);
	

	//Endpoint management
	int EndpointCreate(std::wstring name,bool audioSupported,bool videoSupported,bool textSupported);
	int EndpointDelete(int endpointId);
	int EndpointSetLocalCryptoSDES(int id,MediaFrame::Type media,const char *suite,const char* key);
	int EndpointSetRemoteCryptoSDES(int id,MediaFrame::Type media,const char *suite,const char* key);
	int EndpointSetRemoteCryptoDTLS(int id,MediaFrame::Type media,const char *setup,const char *hash,const char *fingerprint);
	int EndpointSetLocalSTUNCredentials(int id,MediaFrame::Type media,const char *username,const char* pwd);
	int EndpointSetRemoteSTUNCredentials(int id,MediaFrame::Type media,const char *username,const char* pwd);
	int EndpointSetRTPProperties(int id,MediaFrame::Type media,const Properties& properties);
	//Endpoint Video functionality
	int EndpointStartSending(int endpointId,MediaFrame::Type media,char *sendVideoIp,int sendVideoPort,RTPMap& rtpMap);
	int EndpointStopSending(int endpointId,MediaFrame::Type media);
	int EndpointStartReceiving(int endpointId,MediaFrame::Type media,RTPMap& rtpMap);
	int EndpointStopReceiving(int endpointId,MediaFrame::Type media);

	int EndpointRequestUpdate(int endpointId,MediaFrame::Type media);
	//Attach intput to
	int EndpointAttachToPlayer(int endpointId,int playerId,MediaFrame::Type media);
	int EndpointAttachToEndpoint(int endpointId,int sourceId,MediaFrame::Type media);
	int EndpointAttachToAudioMixerPort(int endpointId,int mixerId,int portId);
	int EndpointDettach(int endpointId,MediaFrame::Type media);
	int EndpointAttachToVideoMixerPort(int endpointId,int mixerId,int portId);
	int EndpointAttachToVideoTranscoder(int endpointId,int transcoderId);

	//AudioMixer management
	int AudioMixerCreate(std::wstring tag);
	int AudioMixerDelete(int mixerId);
	//AudioMixer port management
	int AudioMixerPortCreate(int mixerId,std::wstring tag);
	int AudioMixerPortSetCodec(int mixerId,int portId,AudioCodec::Type codec);
	int AudioMixerPortDelete(int mixerId,int portId);
	//Filters type: 0=pre 1=post
	//int AudioMixerPortAddFilter(int mixerId,int portId,int type,...);
	//int AudioMixerPortUpdatedFilter(int mixerId,int portId,...);
	//int AudioMixerPortDeleteFilter(int mixerId,int portId,int filterId);
	//int AudioMixerPortClearFilters(int mixerId,int portId);
	//Port Attach  to
	int AudioMixerPortAttachToEndpoint(int mixerId,int portId,int endpointId);
	int AudioMixerPortAttachToPlayer(int mixerId,int portId,int playerId);
	int AudioMixerPortDettach(int mixerId,int portId);

        int AudioTranscoderCreate(std::wstring tag);
        int AudioTranscoderDelete(int transcoderId);
        AudioTranscoder * GetAudioTranscoder(int transcoderId);
        
	//Video Mixer management
	int VideoMixerCreate(std::wstring tag);
	int VideoMixerDelete(int mixerId);
	//Video mixer port management
	int VideoMixerPortCreate(int mixerId,std::wstring tag,int mosiacId);
	int VideoMixerPortSetCodec(int mixerId,int portId,VideoCodec::Type codec,int size,int fps,int bitrate,int intraPeriod);
	int VideoMixerPortDelete(int mixerId,int porId);
	int VideoMixerPortAttachToEndpoint(int mixerId,int portId,int endpointId);
	int VideoMixerPortAttachToPlayer(int mixerId,int portId,int playerId);
	int VideoMixerPortDettach(int mixerId,int portId);
	//Video mixer mosaic management
	int VideoMixerMosaicCreate(int mixerId,Mosaic::Type comp,int size);
	int VideoMixerMosaicDelete(int mixerId,int portId);
	int VideoMixerMosaicSetSlot(int mixerId,int mosaicId,int num,int portId);
	int VideoMixerMosaicSetCompositionType(int mixerId,int mosaicId,Mosaic::Type comp,int size);
	int VideoMixerMosaicSetOverlayPNG(int mixerId,int mosaicId,const char* overlay);
	int VideoMixerMosaicResetSetOverlay(int mixerId,int mosaicId);
	int VideoMixerMosaicAddPort(int mixerId,int mosaicId,int portId);
	int VideoMixerMosaicRemovePort(int mixerId,int mosaicId,int portId);

	int VideoTranscoderCreate(std::wstring tag);
	int VideoTranscoderDelete(int transcoderId);
	int VideoTranscoderSetCodec(int transcoderId,VideoCodec::Type codec,int size,int fps,
				    int bitrate,int intraPeriod, Properties & props);					
	int VideoTranscoderFPU(int transcoderId);
	int VideoTranscoderAttachToEndpoint(int transcoderId,int endpointId);
	int VideoTranscoderDettach(int transcoderId);

	//Events
	virtual void onEndOfFile(Player *player,void* param);

	//Getters
	std::wstring& GetTag() { return tag;	}
	
	Endpoint* 				GetEndpoint(int endpointId) ;
	Player*					GetPlayer(int playerId) ;
	JSR309EventContext*		GetEventContext(int EventContextId) ;
	/**
	 *  Callback that associate an actual media connection (here a web socket)
	 *  with an endpoint. The endpoint needs to be properly prepared by calling
	 *  ConfigureMediaConnection() an specify Web socket as transport protocol.
	 *  It shall also associate a "token" that will serve as security but also
	 *  as key to identify the concerned endpoint and the concerned media and
	 *  role
	 */
	int onNewMediaConnection(WebSocket *ws, const std::string & token);
	
	/**
	 *  Configure / change the protocol of a media for a given EndPoint.
	 *  By default, RTP media is used as transport but using this function
	 *  one can replace RTP by another media transport
	 *  @param endpointId:  id of enpoint to configure
	 *  @param media: Audio, Video, Text or Application. If selected protocol is RTMP
	 *  all media are automatically reconfigured
	 *  @param role: only for Video media, can select main video or slides
	 *  @param proto: media transport protocol expected for this media and role.
	 *  @param token: token to identify the triplet (endpoint, media, role)
	 *  This parameter is only useful for WebSocket and RTMP. It is ignored in
	 *  case of other protocol
	 *  @param expectedPayload: string that describe what kind of media is transported.
	 *  Unused for now.
	 *  @return
	 *  1: the endoint has been successfully configured
	 *  0:the endpoint could not be configured: unexpected error or bad state.
	 *  -1: no such endpoint.
	 *  -2: protocol not yet supported
	 *  -3: media or role not supported for this protocol	 
	 */
	int ConfigureMediaConnection( int endpointId, MediaFrame::Type media, MediaFrame::MediaRole role, 
				      MediaFrame::MediaProtocol proto, const char * token, const char * expectedPayload );
private:
	struct MediaCnxToken
	{
	    int endpointId;
	    MediaFrame::Type media;
	    MediaFrame::MediaRole role;
	    MediaFrame::MediaProtocol proto;
	};
	


	typedef std::map<int,Endpoint*> Endpoints;
	typedef std::map<int,Recorder*> Recorders;
	typedef std::map<int,Player*> Players;
	typedef std::map<int,AudioMixerResource*> AudioMixers;
	typedef std::map<int,VideoMixerResource*> VideoMixers;
    typedef std::map<int,AudioTranscoder*> AudioTranscoders;
	typedef std::map<int,VideoTranscoder*> VideoTranscoders;
	typedef std::map<std::string, MediaCnxToken> Tokens;
	typedef std::map<int, JSR309EventContext*> EventContexts;
private:
	std::wstring tag;
	
	MediaSession::Listener *listener;
	void* param;

	Endpoints endpoints;
	int maxEndpointId;

	Players players;
	int maxPlayersId;

	Recorders recorders;
	int maxRecordersId;

	AudioMixers	audioMixers;
	int maxAudioMixerId;

	VideoMixers	videoMixers;
	int maxVideoMixerId;

        AudioTranscoders audioTranscoders;
	VideoTranscoders videoTranscoders;
	int maxVideoTranscoderId;
	
	Tokens		tokens;
	
	EventContexts eventContexts;
	int maxEventContextId;
 
};

class PlayerEndOfFileEvent: public JSR309Event
{
public:
	PlayerEndOfFileEvent(std::wstring &playerTag)
	{
		//Get session tag
		UTF8Parser playerTagParser(playerTag);

		//Serialize
		DWORD playerLen  = playerTagParser.Serialize(this->playerTag,1024);

		//Set end
		this->playerTag[playerLen] = 0;
	}
	
	virtual xmlrpc_value* GetXmlValue(xmlrpc_env *env)
	{
		BYTE sessTag[1024];
		UTF8Parser sessTagParser(sessionTag);
		DWORD sessLen = sessTagParser.Serialize(sessTag,1024);
		sessTag[sessLen] = 0;
		
		return xmlrpc_build_value(env,"(iss)",(int)JSR309Event::PlayerEndOfFileEvent,sessTag,playerTag);
	}
private:
	
	BYTE playerTag[1024];
};


#endif	/* MEDIASESSION_H */

