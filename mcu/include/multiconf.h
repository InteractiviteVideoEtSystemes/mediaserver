#ifndef _MULTICONF_H_
#define _MULTICONF_H_
#include "videomixer.h"
#include "audiomixer.h"
#include "textmixer.h"
#include "videomixer.h"
#include "participant.h"
#include "FLVEncoder.h"
#include "broadcastsession.h"
#include "mp4player.h"
#include "mp4recorder.h"
#include "audioencoder.h"
#include "textencoder.h"
#include "rtmpnetconnection.h"
// #include "websockets.h"
#include "appmixer.h"
#include "shareddocmixer.h"
#include <map>
#include <string>

class RTMPParticipant;

class MultiConf :
	public RTMPNetConnection,
	public Participant::Listener,
	public RTMPClientConnection::Listener
{
public:
	static const int AppMixerId = 1;
	static const int SharedDocMixerId = 2;
	static const int RecorderId = 10;
	
public:
	typedef std::map<std::string,MediaStatistics> ParticipantStatistics;
public:
	class NetStream : public RTMPNetStream
	{
	public:
		NetStream(DWORD streamId,MultiConf *conf,RTMPNetStream::Listener *listener);
		virtual ~NetStream();
		virtual void doPlay(std::wstring& url,RTMPMediaStream::Listener *listener);
		virtual void doPublish(std::wstring& url);
		virtual void doSeek(DWORD time);
		virtual void doClose(RTMPMediaStream::Listener *listener);
                virtual void doPause();
                virtual void doResume();
                virtual void doCommand(RTMPCommandMessage *cmd);
	protected:
		void Close();
	private:
		MultiConf *conf;
                RTMPParticipant * part;
		bool opened;
	};

	class Listener
	{
	public:
		//Virtual desctructor
		virtual ~Listener(){};
		virtual void onParticipantRequestFPU(MultiConf *conf,int partId,void *param) = 0;
		virtual void onParticipantRequestDocSharing(MultiConf *conf,int partId,std::wstring status, void *param) = 0;
		
	};
	
public:
	MultiConf(const std::wstring& tag);
	~MultiConf();

	int Init(int vad,DWORD rate);
	int End();

	void SetListener(Listener *listener,void* param);

	int CreateMosaic(Mosaic::Type comp,int size);
	int SetMosaicOverlayImage(int mosaicId,const char* filename);
	int ResetMosaicOverlay(int mosaicId);
	int DeleteMosaic(int mosaicId);
	int CreateSidebar();
	int DeleteSidebar(int sidebarId);
	int CreateParticipant(int mosaicId,int sidebarId,std::wstring name,Participant::Type type);
	int StartRecordingParticipant(int partId,const char* filename);
	int StopRecordingParticipant(int partId);
	int SendFPU(int partId);
	int SetMute(int partId,MediaFrame::Type media,bool isMuted);
	void SetVADMode(int mode);
	ParticipantStatistics* GetParticipantStatistic(int partId);
	int SetParticipantMosaic(int partId,int mosaicId);
	int SetParticipantSidebar(int partId,int sidebarId);
	int SetParticipantDisplayName(int mosaicId, int partId, const char *name, int scriptCode);
	int DeleteParticipant(int partId);

	int CreatePlayer(int privateId,std::wstring name);
	int StartPlaying(int playerId,const char* filename,bool loop);
	int StopPlaying(int playerId);
	int DeletePlayer(int playerId);

	int AppMixerDisplayImage(const char* filename);
	// int AppMixerWebsocketConnectRequest(int partId,WebSocket *ws,bool isPresenter);
	
	int SetCompositionType(int mosaicId,Mosaic::Type comp,int size);
	int SetMosaicSlot(int mosaicId,int num,int id);
	int AddMosaicParticipant(int mosaicId,int partId);
	int RemoveMosaicParticipant(int mosaicId,int partId);
	int AddSidebarParticipant(int sidebar,int partId);
	int RemoveSidebarParticipant(int sidebar,int partId);

	int GetMosaicPositions(int mosaicId,std::list<int> &positions);
	
	int StartSending(int partId,MediaFrame::Type media,char *sendIp,int sendPort,RTPMap& rtpMap,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	int StopSending(int partId,MediaFrame::Type media,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	int StartReceiving(int partId,MediaFrame::Type media,RTPMap& rtpMap,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN,int confID= 0, MediaFrame::MediaProtocol proto = MediaFrame::TCP);
	int StopReceiving(int partId,MediaFrame::Type media,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	int SetLocalCryptoSDES(int id,MediaFrame::Type media,const char *suite,const char* key, MediaFrame::MediaRole role);
	int SetRemoteCryptoSDES(int id,MediaFrame::Type media,const char *suite,const char* key, MediaFrame::MediaRole role,int keyRank=0);
	int SetLocalSTUNCredentials(int id,MediaFrame::Type media,const char *username,const char* pwd, MediaFrame::MediaRole role);
	int SetRemoteSTUNCredentials(int id,MediaFrame::Type media,const char *username,const char* pwd, MediaFrame::MediaRole role);
	int SetRemoteCryptoDTLS(int id,MediaFrame::Type media,const char *setup,const char *hash,const char *fingerprint);	
	int SetRTPProperties(int id,MediaFrame::Type media,const Properties& properties,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	int SetParticipantBackground(int id, const char * filename);
	int SetParticipantOverlay(int mosaicId, int id, const char * filename);
	
	
	int AcceptDocSharingRequest(int confId,int partId);
	int RefuseDocSharingRequest(int confId,int partId);
	int StopDocSharing(int confId,int partId);
	int SetDocSharingMosaic(int mosaicId, int partId=0);
	
	int SetVideoCodec(int partId,int codec,int mode,int fps,int bitrate,int intraPeriod,const Properties &properties,MediaFrame::MediaRole role = MediaFrame::VIDEO_MAIN);
	int SetAudioCodec(int partId,int codec,const Properties& properties);
	int SetTextCodec(int partId,int codec);
	int SetAppCodec(int confId, int partId,int codec);

	int  StartBroadcaster(int mosaicId, int sidebarId);
	int  StartRecordingBroadcaster(const char* filename,int mosaicId, int sidebarId);
	int  StopRecordingBroadcaster();
	int  StartPublishing(const char* server,int port, const char* app,const char* name);
	int  StopPublishing(int id);
	int  StopBroadcaster();

	bool AddParticipantInputToken(int partId,const std::wstring &token);
	bool AddParticipantOutputToken(int partId,const std::wstring &token);
	bool AddBroadcastToken(const std::wstring &token);

	RTMPParticipant* ConsumeParticipantOutputToken(const std::wstring &token);
	RTMPMediaStream::Listener* ConsumeParticipantInputToken(const std::wstring &token);
	RTMPMediaStream* ConsumeBroadcastToken(const std::wstring &token);

	int GetNumParticipants() { return participants.size(); }
	std::wstring& GetTag() { return tag;	}

	/** Participants event */
	void onRequestFPU(Participant *part);
	void onRequestDocSharing(int partId, std::wstring status);

	/** RTMPNetConnection */
	//virtual void Connect(RTMPNetConnection::Listener* listener); -> Not needed to be overriden yet
	virtual RTMPNetStream* CreateStream(DWORD streamId,DWORD audioCaps,DWORD videoCaps,RTMPNetStream::Listener* listener);
	virtual void DeleteStream(RTMPNetStream *stream);
	//virtual void Disconnect(RTMPNetConnection::Listener* listener);  -> Not needed to be overriden yet

	/** RTMPClientConnection for pubblishers*/
	virtual void onConnected(RTMPClientConnection* conn);
	virtual void onNetStreamCreated(RTMPClientConnection* conn,RTMPClientConnection::NetStream *stream);
	virtual void onCommandResponse(RTMPClientConnection* conn,DWORD id,bool isError,AMFData* param);
	virtual void onDisconnected(RTMPClientConnection* conn);

        /**
         * Dump participant info as printable string
         * @param partId
         * @param info info to printout
         * @return HTTP error code
         */
        int DumpParticipantInfo(int partId, std::string & info);
        int DumpMixerInfo(int id, MediaFrame::Type media, std::string & info);
        int DumpInfo(std::string & info); 
private:
	Participant *GetParticipant(int partId);
	Participant *GetParticipant(int partId,Participant::Type type);
	int DestroyParticipant(int partId,Participant* part);
private:
	struct PublisherInfo
	{
		DWORD			id;
		std::wstring		name;
		RTMPClientConnection*	conn;
		RTMPClientConnection::NetStream * stream;
	};
private:
	typedef std::map<int,Participant*> Participants;
	typedef std::set<std::wstring> BroadcastTokens;
	typedef std::map<std::wstring,DWORD> ParticipantTokens;
	typedef std::map<int, MP4Player*> Players;
	typedef std::map<int, PublisherInfo> Publishers;

private:
	ParticipantTokens	inputTokens;
	ParticipantTokens	outputTokens;
	BroadcastTokens		tokens;
	//Atributos
	int		inited;
	int		maxId;
	std::wstring	tag;

	Listener *listener;
	void* param;

	//Los mixers
	VideoMixer videoMixer;
	AudioMixer audioMixer;
	TextMixer  textMixer;
	AppMixer   appMixer;
	SharedDocMixer sharedDocMixer;
	
	//Lists
	Participants		participants;
	Players			players;

	int			watcherId;
	int			broadcastId;
	FLVEncoder		flvEncoder;
	FLVEncoder		recEncoder;
	AudioEncoderWorker	audioEncoder;
	TextEncoder		textEncoder;
	BroadcastSession	broadcast;
	RecorderControl*	recorder;
	Publishers		publishers;
	int			maxPublisherId;

	Use			participantsLock;
};

#endif
