/* 
 * File:   mediabridgesession.h
 * Author: Sergio
 *
 * Created on 22 de diciembre de 2010, 18:20
 */

#ifndef _MEDIABRIDGESESSION_H_
#define	_MEDIABRIDGESESSION_H_
#include "rtpsession.h"
#include "rtmpnetconnection.h"
#include "rtmpstream.h"
#include "codecs.h"
#include "mp4recorder.h"
#include "waitqueue.h"
#include "RTPSmoother.h"
#include "xmlstreaminghandler.h"
#include "pipetextinput.h"
#include "text.h"
#include "redcodec.h"
#include "textstream.h"
#include <deque>
#include <set>
#include "pipeaudioinput.h"
#include "pipeaudiooutput.h"

class MediaBridgeSession  :
	public RTMPNetConnection,
	public RTMPMediaStream,
	public RTMPMediaStream::Listener,
        public TextOutput
{
public:
	enum Events
	{
		OnTextReceivedEvent = 1,
		OnCmdReceivedEvent = 2,
		OnCSIANSIReceivedEvent = 3
	};
	typedef enum 
	{
		NIL=0,
		NL, 
		BS, 
		BEL,
		BOM,
		ESC
	} TextCommands;

	class NetStream : public RTMPNetStream
	{
	public:
		NetStream(DWORD streamId,MediaBridgeSession *sess,RTMPNetStream::Listener *listener);
		virtual ~NetStream();
		virtual void doPlay(std::wstring& url,RTMPMediaStream::Listener *listener);
		virtual void doPublish(std::wstring& url);
		virtual void doClose(RTMPMediaStream::Listener *listener);
	private:
		void Close();
	private:
		MediaBridgeSession *sess;
	};
public:
	MediaBridgeSession();
	~MediaBridgeSession();

	bool Init(XmlStreamingHandler *eventMngr);

	//Video RTP
	int StartSendingVideo(char *sendVideoIp,int sendVideoPort,RTPMap& rtpMap);
	int SetSendingVideoCodec(VideoCodec::Type codec);
	int SendFPU();
	int StopSendingVideo();
	int StartReceivingVideo(RTPMap& rtpMap);
	int StopReceivingVideo();
	//Audio RTP
	int StartSendingAudio(char *sendAudioIp,int sendAudioPort,RTPMap& rtpMap);
	int SetSendingAudioCodec(AudioCodec::Type codec);
	int StopSendingAudio();
	int StartReceivingAudio(RTPMap& rtpMap);
	int StopReceivingAudio();
	//T140 Text RTP
	int StartSendingText(char *sendTextIp,int sendTextPort,RTPMap& rtpMap);
        int SetSendingTextCodec(TextCodec::Type codec);
	int StopSendingText();
	int StartReceivingText(RTPMap& rtpMap);
	int StopReceivingText();
	
	void SendTextInput(char* mess);
	void SendSpecial(char* cmd, int nbCmd=1);
	int getQueueId();
	void setQueueId(int p_queueId);
	int getSessionId();
	void setSessionId(int p_sessionId);
	bool End();

	void AddInputToken(const std::wstring &token);
	void AddOutputToken(const std::wstring &token);

	bool ConsumeOutputToken(const std::wstring &token);
	bool ConsumeInputToken(const std::wstring &token);

	/** RTMPNetConnection */
	//virtual void Connect(RTMPNetConnection::Listener* listener); -> Not needed to be overriden yet
	virtual RTMPNetStream* CreateStream(DWORD streamId,DWORD audioCaps,DWORD videoCaps,RTMPNetStream::Listener* listener);
	virtual void DeleteStream(RTMPNetStream *stream);
	//virtual void Disconnect(RTMPNetConnection::Listener* listener);  -> Not needed to be overriden yet

	/* Overrride from RTMPMediaStream*/
	virtual DWORD AddMediaListener(RTMPMediaStream::Listener *listener);
	virtual DWORD RemoveMediaListener(RTMPMediaStream::Listener *listener);

	//RTMPMediaStream Listener
	virtual void onAttached(RTMPMediaStream *stream);
	virtual bool onMediaFrame(DWORD id,RTMPMediaFrame *frame);
	virtual void onMetaData(DWORD id,RTMPMetaData *meta);
	virtual void onCommand(DWORD id,const wchar_t *name,AMFData* obj);
	virtual void onStreamBegin(DWORD id);
	virtual void onStreamEnd(DWORD id);
	virtual void onStreamReset(DWORD id);
	virtual void onDetached(RTMPMediaStream *stream);
	
        /* TextOutput*/
        virtual int SendFrame(TextFrame &frame);
        
protected:
	int RecVideo();
	int RecAudio();
	int SendVideo();
	int SendAudio();
	int DecodeAudio();

private:
	void ConfigureAudioInput();
	void SendTextToFlashClient( std::wstring wstext );
	void SendTextToHtmlClient( std::wstring  wstext );

	static void* startReceivingVideo(void *par);
	static void* startReceivingAudio(void *par);

	static void* startSendingVideo(void *par);
	static void* startSendingAudio(void *par);
	static void* startDecodingAudio(void *par);

private:
	typedef std::set<std::wstring> ParticipantTokens;

private:
	//Tokens
	ParticipantTokens inputTokens;
	ParticipantTokens outputTokens;
	//Bridged sessions
	RTMPMetaData 	*meta;
	RTPSession      rtpAudio;
	RTPSession      rtpVideo;
	RTPSmoother	smoother;
        RedundentCodec  redCodec;
        
	WaitQueue<RTMPVideoFrame*> videoFrames;
	PipeAudioInput *audioInput;
	PipeAudioOutput *audioOutput;
        
        TextStream      textStream;
	PipeTextInput  *textInput;
	
	VideoCodec::Type rtpVideoCodec;
	AudioCodec::Type rtpAudioCodec;
	TextCodec::Type  rtpTextCodec;
	BYTE             t140Codec;

	AudioEncoder *rtpAudioEncoder;
	AudioDecoder *rtpAudioDecoder;
	AudioEncoder *rtmpAudioEncoder;
	AudioDecoder *rtmpAudioDecoder;

	//Las threads
	pthread_t 	recVideoThread;
	pthread_t 	recAudioThread;
	pthread_t 	sendVideoThread;
	pthread_t 	sendAudioThread;
	pthread_t	decodeAudioThread;
	pthread_mutex_t	mutex;
	
	XmlStreamingHandler	*eventMngr;

	//Controlamos si estamos mandando o no	
	enum MediaState
	{
		Stopped = 0,
		Starting = 1,
		Running = 2,
                Stopping = 3
	};
	enum MediaState	sendingVideo;
	enum MediaState receivingVideo;
	enum MediaState	sendingAudio;
	enum MediaState	receivingAudio;
	enum MediaState	sendingText;
	enum MediaState receivingText;
	bool	inited;
	bool	waitVideo;
	bool	sendFPU;
	timeval	first;
	int 	queueId;
	int 	sessionId;
};

class OnTextReceivedEvent: public XmlEvent
{
public:
	OnTextReceivedEvent(int p_session_id , std::wstring p_message);
	virtual xmlrpc_value* GetXmlValue(xmlrpc_env *env);

private:
	BYTE message[PATH_MAX];
	int	 session_id;
};

class OnCmdReceivedEvent: public XmlEvent
{
public:
	OnCmdReceivedEvent(int p_session_id , MediaBridgeSession::TextCommands p_cmd);
	virtual xmlrpc_value* GetXmlValue(xmlrpc_env *env);

private:
	MediaBridgeSession::TextCommands cmd;
	int	 session_id;
};

class OnCSIANSIReceivedEvent: public XmlEvent
{
public:
	OnCSIANSIReceivedEvent(int p_session_id ,  std::wstring p_message);
	virtual xmlrpc_value* GetXmlValue(xmlrpc_env *env);

private:
	BYTE CSIANSIMessage[PATH_MAX];
	int	 session_id;
};

#endif	/* MEDIABRIDGESESSION_H */

