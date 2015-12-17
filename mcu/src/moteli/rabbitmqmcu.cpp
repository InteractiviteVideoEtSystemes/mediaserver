#include <iostream>
#include "rabbitmqmcu.h"
#include "mcu.pb.h"
#include "multiconf.h"
#include "mcu.h"
#include "log.h"
#include "media.h"

#define ENTETE_BULL 1

using namespace moteli::mcu;

McuRabbitHandler::McuRabbitHandler(MCU *mcu)
{
    this->mcu = mcu;
}

int McuRabbitHandler::onMessage( AMQPMessage *m)
{
        uint32_t len = 0;
        char * data = m->getMessage(&len);
	MsgHeader h;
	
	if ( m->getHeader("Content-type") == "application/moteli+protobuf")
	{
		Log(" >onMessage: received a control message for the MCU.\n");
	}
	else
	{
		Error("-rqserver: Ignoring message of type: %s.\n",m->getHeader("Content-type").c_str());
		return 0;
	}
	
	h.replyTo = m->getHeader("Reply-to");
	if (h.replyTo.empty() )
	{
	    cout << "Message has no Reply-To header. Trying with reply_to " << endl;
	    h.replyTo = m->getHeader("reply_to");
	    if (h.replyTo.empty() )
	    {
	        Error(" <onMessage: Message has no reply_to header. Ignoring\n");
		return 0;
	    }
	}
	
	h.rqCorId = m->getHeader("correlation_id");

	h.messageId = m->getHeader("message_id");
	if ( h.messageId.empty() )
	{
	        Error(" <onMessage: Message has no message_id header. Ignoring\n");
		return 0;
	};

        if (data)
	{
	    if ( len > 3 )
	    {
		char tagLenStr[4];
	        unsigned short msgTagLen;

		strncpy(tagLenStr, data, 2);
		tagLenStr[2] = '\0';
		msgTagLen = atoi(tagLenStr);

		if ( msgTagLen < 40 && msgTagLen > 0 )
		{
		    if ( len <= msgTagLen + 2 )
		    {
			cout << "message payload is too small. Inconsitent msg. Dropping it." << endl;
			return 0;
		    }
		    string msgType( data + 2, msgTagLen );
		    cout << "Got protobuf msg type: " << msgType << endl;

		    // On prent le reste du msg
		    const char * protobuf = (const char *) data + 2 + msgTagLen;
		    int protobufLen = len - msgTagLen - 2;

		    if ( msgType.compare("BindReq") == 0 )
		    {
			return onBindReq( protobuf, protobufLen, h );
		    }
		    else if ( msgType.compare("CreateMcuObjectReq") == 0 )
		    {
			return onCreateMcuObject( protobuf, protobufLen, h);
		    }
		    else if ( msgType.compare("DeleteMcuObjectReq") == 0 )
		    {
			return onDeleteMcuObject( protobuf, protobufLen, h);
		    }
		    else if ( msgType.compare("SetMediaPropertiesReq") == 0 )
		    {
			return onSetMediaPropertiesReq( protobuf, protobufLen, h);
		    }
		    else if ( msgType.compare("StartMediaReq") == 0 )
		    {
			return onStartMediaReq( protobuf, protobufLen, h);
		    }
		    else if ( msgType.compare("StopMediaReq") == 0 )
		    {
			return onStopMediaReq( protobuf, protobufLen, h);
		    }
		    else if ( msgType.compare("AddParticipantsToMixerReq") == 0 )
		    {
			return onAddToMixerReq( protobuf, protobufLen, h);
		    }
		    else
		    {
		        Error(" <onMessage: Message %s not supported. Ignoring\n", msgType.c_str());
			return ReplyError( h, NULL, ErrorRsp_ErrCode_NOT_SUPPORTED,
				  "MCU does not support this message.");		        
		    }
		}
		else
		{
		     cout << "Inconsistent message header. msgTypeLen = " << msgTagLen
		          << ".  msgSize = " << len << endl;
		}
	    }
	    else
	    {
		cout << "message payload is too small. Inconsitent msg. Dropping it." << endl;
	    }
	}
	else
	{
		cout << "no payload for this msg" << endl;
	}
	return 0;
}

int McuRabbitHandler::onBindReq( const char * payload, size_t len, MsgHeader & h )
{
    moteli::mcu::BindReq req;
    moteli::mcu::BindResp resp;
    
    if (req.ParseFromArray( payload, len ))
    {
        int corId = 0;
	if ( req.has_correlationid() )
	{
	     corId = req.correlationid();
	}
	cout << "BindRequest received with correlationId = " << corId << endl;
	
	resp.set_correlationid( corId );
	return SendMessage(h, &resp);
    }
    return 0;
}

int McuRabbitHandler::onCreateMcuObject( const char * payload, size_t len,  MsgHeader & h )
{
    moteli::mcu::CreateMcuObjectReq req;

    if (req.ParseFromArray( payload, len ))
    {
        int corId = 0;
	McuObjectId concernedObj;
        if ( req.has_correlationid() )
        {
             corId = req.correlationid();
        }

	switch( req.typeofobjtocreate() )
	{
	    case McuObjectId_ObjectType_CONFERENCE:
		return CreateConference( &req, h );

	    case McuObjectId_ObjectType_PARTICIPANT:
		return CreateParticipant( &req, h );

	    default:
		if ( req.has_parentobj() )
		{
		     concernedObj.CopyFrom( req.parentobj() );
		}
		else
		{
		     concernedObj.set_confid(0);
		     concernedObj.set_partid(0);
		}
		concernedObj.set_objtype( req.typeofobjtocreate() );
		cout << "type d'objet a creer " << req.typeofobjtocreate() << " pas supporte." << endl;
	        return ReplyError( h, &concernedObj, ErrorRsp_ErrCode_NOT_SUPPORTED,
				  "MCU cannot create this kind of object.");
	}
    }
    else
    {
            return ReplyError( h, NULL, ErrorRsp_ErrCode_INVALID_MESSAGE,
				  "Cannot parse CreateMcuObjectReq message");
    }
    
    return 0;
}

int McuRabbitHandler::onDeleteMcuObject( const char * payload, size_t len,  MsgHeader & h )
{
    moteli::mcu::DeleteMcuObjectReq req;
    MultiConf *conf = NULL;

    if (req.ParseFromArray( payload, len ))
    {
        int corId = 0;
	McuObjectId const & concernedObj = req.objid();
	int ret;
	
        if ( req.has_correlationid() )
        {
             corId = req.correlationid();
        }

	switch( concernedObj.objtype() )
	{
	    case McuObjectId_ObjectType_CONFERENCE:
	        if ( ! mcu->DeleteConference(concernedObj.confid()) )
		    return ReplyError( h, &concernedObj, ErrorRsp_ErrCode_OBJ_NOT_FOUND,
				      "DeleteMcuObjectReq: no such conference to delete.");
		break;

	    case McuObjectId_ObjectType_PARTICIPANT:
		if(!mcu->GetConferenceRef(concernedObj.confid(),&conf))
		{
		    return ReplyError( h, &concernedObj, 
			   ErrorRsp_ErrCode_OBJ_NOT_FOUND,
			   "DeleteMcuObjectReq: the conference associated with the participant to delete does not exist" );
                }
		ret = conf->DeleteParticipant( concernedObj.partid() );
		mcu->ReleaseConferenceRef( concernedObj.confid() );
		if (!ret)
		{
		    return ReplyError( h, &concernedObj, 
			   ErrorRsp_ErrCode_OBJ_NOT_FOUND,
			   "DeleteMcuObjectReq: failed to delete participant." );
		}
		break;
		
	    default:
	        return ReplyError( h, &concernedObj, ErrorRsp_ErrCode_NOT_SUPPORTED,
				  "DeleteMcuObjectReq: cannot delete this kind of object.");
	}
    }
    else
    {
         return ReplyError( h, NULL, ErrorRsp_ErrCode_INVALID_MESSAGE,
			    "Cannot parse DeleteMcuObjectReq message");
    }
    
    return 0;
}

/**
 * CreateConference()
 *
 * self explanatory
 */
int McuRabbitHandler::CreateConference( void *r, MsgHeader & h )
{
    MultiConf *conf = NULL;
    CreateMcuObjectResp resp;
    CreateMcuObjectReq *req = (CreateMcuObjectReq *) r;
    UTF8Parser tagParser;
    int vad = 0;
    int rate = 0;
    int queueId = 0;


    //Verifier que le parent est null
    if ( req->has_parentobj() )
    {
	cout << "Creation conference invalide: la requete contient un objet parent" << endl;
	return  ReplyError( h, NULL, 
			   ErrorRsp_ErrCode_INVALID_PARAMETER, 
			   "CreateMcuObjectReq cannot reference a parent object when creating a conference." );
    }

    if ( !req->has_conferenceparams() )
    {
	return  ReplyError( h,
			    NULL, 
			    ErrorRsp_ErrCode_INVALID_PARAMETER,
			    "Missing confrence parameter" );
    }
    else
    {
       const ConferenceParams & params = req->conferenceparams();

	vad = params.voiceactivitydetectionon() ? 2 : 0;
        tagParser.Parse( (BYTE *) params.name().c_str(),  params.name().length() );
	queueId = params.has_eventlistenerid() ? params.eventlistenerid() : 0;
	rate = params.has_audiorate() ? params.audiorate() : 16000;
    }

    int confId = mcu->CreateConference(tagParser.GetWString(),queueId);

    if  (confId <= 0)
    {
	return ReplyError( h, NULL, 
			   ErrorRsp_ErrCode_PROCESSING_ERR,
			   "CreateMcuObjectReq: failed to create conference." );
    }

    McuObjectId * objId = new McuObjectId();
    objId->set_objtype( McuObjectId_ObjectType_CONFERENCE );
    objId->set_confid( confId );

    if(!mcu->GetConferenceRef(confId,&conf))
    {
	return ReplyError( h, objId, 
			   ErrorRsp_ErrCode_OBJ_NOT_FOUND,
			   "CreateMcuObjectReq: conference has disapeared before intialization." );
    }

    int res = conf->Init(vad,rate);

     //Liberamos la referencia
     mcu->ReleaseConferenceRef(confId);

        //Salimos
    if(!res)
    {
	return ReplyError( h, objId, 
			   ErrorRsp_ErrCode_NOT_ENOUGH_RESSOURCE,
			   "CreateMcuObjectReq: failed to initializz conference." );
    }

    resp.set_correlationid( 0 );
    resp.set_allocated_objid( objId );
    return SendMessage(h, &resp);
}

int McuRabbitHandler::CreateParticipant( void *r, MsgHeader & h )
{
    MultiConf *conf = NULL;
    CreateMcuObjectResp resp;
    int confId;
    int mosaicId;
    int sidebarId;
    char *name;
    enum Participant::Type type;
    UTF8Parser parser;
 
    CreateMcuObjectReq *req = (CreateMcuObjectReq *) r;
    //Verifier que le parent est null
    if ( !req->has_parentobj() )
    {
	cout << "Creation participant invalide: pas de conference de rattachement" << endl;
	return ReplyError( h, NULL, 
			   ErrorRsp_ErrCode_INVALID_PARAMETER,
			   "CreateMcuObjectReq: cannot create participant. no conference specified." );
    }

    // TODO: verifier qu'il sagit bien d'une conference

    // Recuperer l'ID de la conf de rattachement
    confId = req->parentobj().confid();
    if(!mcu->GetConferenceRef(confId,&conf))
	return  ReplyError( h, &req->parentobj(), 
			    ErrorRsp_ErrCode_INVALID_PARAMETER,
			    "CreateParticipant: conf does not exist");


    int partId = 0;

    if ( req->has_participantparams() )
    {
         const CreateMcuObjectReq_ParticipantParams & params = req->participantparams();

	switch( params.mediaproto() )
	{
        case moteli::mcu::RTP:
            type = Participant::RTP;
            break;

        case moteli::mcu::RTMP:
            type = Participant::RTMP;
            break;

        case moteli::mcu::SKYPE:
        default:
	    mcu->ReleaseConferenceRef(confId);
            return  ReplyError( h, &req->parentobj(),  ErrorRsp_ErrCode_INVALID_PARAMETER, "Unsupported participant type");
        }

        parser.Parse( (BYTE *) params.name().c_str(), params.name().length() );
        partId = conf->CreateParticipant(
                               params.mosaicid(),
                                params.sidebarid(),
                                parser.GetWString(),
                                type);

	mcu->ReleaseConferenceRef(confId);
    }
    else
    {
	mcu->ReleaseConferenceRef(confId);
	return  ReplyError( h, &req->parentobj(),  ErrorRsp_ErrCode_INVALID_PARAMETER, "Missing participant parameter");
    }

    if (partId == 0)
	    return  ReplyError( h, &req->parentobj(),  ErrorRsp_ErrCode_OVER_CAPACITY, "failed to create part");

    resp.set_correlationid( 0 );
    McuObjectId * objId = new McuObjectId();
    objId->set_objtype( McuObjectId_ObjectType_PARTICIPANT );
    objId->set_confid( confId );
    objId->set_partid( partId );
    resp.set_allocated_objid( objId );
    return SendMessage(h, &resp);
}

/**
 * ReplyError
 *
 * Envoyer un message d'erreur
 * @param rqCorId: correlation Id of rabbit mq enveloppe
 * @pramm corId: correlation Id in protobuf message
 */
int  McuRabbitHandler::ReplyError(  const MsgHeader & h, const void * o, int err, const char * desc )
{
	McuObjectId * objId = ( McuObjectId * ) o;
	moteli::mcu::ErrorRsp merr;

	if ( objId == NULL )
	{
	    // Envoyer un obj ID conf 0
	    objId = new McuObjectId();
	    objId->set_objtype( McuObjectId_ObjectType_CONFERENCE );
	    objId->set_confid(0);
	}
	else
	{
	    McuObjectId * clone = new McuObjectId(*objId);
	    objId = clone;
	}

	Error("-rqserver: sending errmsg message: err=%d msg=[%s] \n", err, desc);
	if ( objId )
	{
	    switch( objId->objtype() )
	    {
	        case McuObjectId_ObjectType_CONFERENCE:
		     Error("-rqserver: concerned object is conference confID=%d.\n", objId->confid());
		     break;
		     
	        case McuObjectId_ObjectType_PARTICIPANT:
		     Error("-rqserver: concerned object is participant confID=%d, partID=%d.\n", objId->confid(), objId->partid());
		     break;
		     
		default:
		     Error("-rqserver: concerned object is ???? confID=%d.\n", objId->confid());
		     break;
	    }
		     
	}
	else
	{
	    Error("-rqserver: no concerned object specified.\n");
	}
	merr.set_allocated_objid( objId );
	merr.set_errcode( (ErrorRsp_ErrCode) err );
	merr.set_message( desc );
	//merr.set_correlationid( 0 );
	return SendMessage(h, &merr);
}

void McuRabbitHandler::SetExchanger(AMQPExchange * p_ex)
{
    if ( p_ex != NULL)
    {
	ex = p_ex;
	ex->setHeader("Content-type", "application/moteli+protobuf");
	ex->setHeader("Delivery-mode", 2);
    }
}
	
int McuRabbitHandler::SendMessage(const MsgHeader & h, void * msg)
{
	google::protobuf::Message * m = (google::protobuf::Message *) msg;
	if (m != NULL && ex != NULL)
	{
		char payload[2000];
		char * pl = payload;
		const google::protobuf::Descriptor * d = m->GetDescriptor();
		size_t typeLen =  d->name().length();
		size_t totalLen =  typeLen + 2 + m->ByteSize();

		// Ahhout entete BULL: 2 permiers octtes = lg en ascii suivi du type en ascii
		// non termine par 0.
		sprintf(pl, "%02u", typeLen );
		strcpy( pl + 2, d->name().c_str() );
			
		if ( m->SerializeToArray(pl + 2 + typeLen, 2000- typeLen -2 ) )
		{			
			ex->setHeader("Content-type", "application/moteli+protobuf" );
			ex->setHeader("Reply-to", privateQueue->getName() );
			ex->setHeader("correlation_id", h.rqCorId);
			ex->setHeader("message_id", h.messageId);
			cout << "envoi d'un msg " << d->name() << " vers la file " << h.replyTo << "." << endl;
			ex->Publish(pl, totalLen, h.replyTo);
			cout << "msg envoye." << endl;
		}
		else
		{
			Error("failed to serialize message %s to send.", d->name().c_str());
			return -2;
		}
	}
	return 0;
}

void McuRabbitHandler::Dump( const char * data, size_t len)
{
    ::Dump((BYTE *) data, len);
}

/**
 * Conversion function. Convert codec ID from MOTELI protocol to internal MCU AudioCodecs
 * @param codec codec ID from protobuf message.
 * @return internal mediaserver codec ID
 **/ 
static enum AudioCodec::Type ConvAudioCodecType( enum moteli::mcu::Codec codec)
{
    switch(codec)
    {
        case moteli::mcu::PCMU:
	    return AudioCodec::PCMU;
	    
	case moteli::mcu::PCMA:
	    return AudioCodec::PCMA;
	    
	case moteli::mcu::G722:
	    return AudioCodec::G722;
	
	default:
	    {
		char errorcodec[80];
		Error("-MOTELI protocol: audio codec %d not (yet) supported.\n", codec);
		sprintf(errorcodec, "Unsupported audio codec %d.", (int) codec);
		throw std::range_error(errorcodec);
	    }
	    break;
    }
    // Should never happen
    return AudioCodec::PCMU;
}

static enum VideoCodec::Type ConvVideoCodecType( enum moteli::mcu::Codec codec)
{
    switch(codec)
    {
        case moteli::mcu::H264:
	    return VideoCodec::H264;
	    
	case moteli::mcu::H263_1996:
	    return VideoCodec::H263_1996;
	    

	default:
	    {
		char errorcodec[80];
		Error("-MOTELI protocol: video codec %d not (yet) supported.\n", codec);
		sprintf(errorcodec, "Unsupported video codec %d.", (int) codec);
		throw std::range_error(errorcodec);
	    }
	    break;
    }
    // Should never happen
    return VideoCodec::H264;
}

static enum TextCodec::Type ConvTextCodecType( enum moteli::mcu::Codec codec)
{
    switch(codec)
    {
        case moteli::mcu::T140:
	    return TextCodec::T140;
	    
	default:
	    {
		char errorcodec[80];
		Error("-MOTELI protocol: text codec %d not (yet) supported.\n", codec);
		sprintf(errorcodec, "Unsupported text codec %d.", (int) codec);
		throw std::range_error(errorcodec);
	    }
	    break;
    }
    // Should never happen
    return TextCodec::T140;
}



/**
 * Conversion function. Convert meida type from MOTELI protocol to internal MCU media type
 * @param media media type  from protobuf message.
 * @return internal mediaserver media type
 **/ 

static MediaFrame::Type ConvMediaType(enum moteli::mcu::MediaType media)
{
    switch(media)
    {
        case moteli::mcu::AUDIO:
	    return MediaFrame::Audio;
	   
	case moteli::mcu::VIDEO:
	    return MediaFrame::Video;

	case moteli::mcu::TEXT:
	    return MediaFrame::Text;

	default:
	    {
		char errorcodec[80];
		Error("-MOTELI protocol: media type %d not (yet) supported.\n", media);
		sprintf(errorcodec, "Unsupported media type %d.", (int) media);
		throw std::range_error(errorcodec);
	    }
	    break;
    }
    // Should never happen
    return MediaFrame::Audio;
}

int McuRabbitHandler::onSetMediaPropertiesReq( const char * payload, size_t len,  MsgHeader & h )
{
    moteli::mcu::SetMediaPropertiesReq req;
    if ( req.ParseFromArray( payload, len ) )
    {
	const McuObjectId & partId = req.partid();
	MultiConf * conf = NULL;
	Properties properties;
	SetMediaPropertiesResp resp;
	
	if ( partId.objtype() != McuObjectId_ObjectType_PARTICIPANT )
	{
	    return  ReplyError( h, &partId, 
			    ErrorRsp_ErrCode_INVALID_PARAMETER,
			    "SetMediaPropertiesReq: partId is not a participant.");
	}
	
	if(!mcu->GetConferenceRef(partId.confid(), &conf))
		return  ReplyError( h, &partId, 
			    ErrorRsp_ErrCode_INVALID_PARAMETER,
			    "SetMediaPropertiesReq: cannot find specified confId");

	if ( !req.has_codecproperty() )
	{
		mcu->ReleaseConferenceRef(partId.confid());
		return  ReplyError( h, &partId, 
			    ErrorRsp_ErrCode_INVALID_PARAMETER,
			    "SetMediaPropertiesReq: missing codec properties");
	}
	
	try
	{
	    // Build property map from protobuf message
	    for (int i=0; i< req.properties_size(); i++)
	    {
	        const moteli::mcu::SetMediaPropertiesReq_MediaProperty & p = req.properties(i);
		
		properties[p.mediapropname()] = p.mediapropvalue();
	    }
	    
	    switch ( req.mediatype() )
	    {
		case moteli::mcu::AUDIO:
		    conf->SetAudioCodec(partId.partid(), ConvAudioCodecType(req.codecproperty().codec()), properties);
		    mcu->ReleaseConferenceRef(partId.confid());
		    resp.set_nbpropapplied(properties.size());		    break;
		
		case moteli::mcu::VIDEO:
		    if ( ! req.has_codecproperty() )
		    {
			 mcu->ReleaseConferenceRef(partId.confid());
                        return  ReplyError( h, &partId,
                            ErrorRsp_ErrCode_INVALID_PARAMETER,
                            "SetMediaPropertiesReq[Video]: no codec property specified.");
		    }
		    else
		    {
			const moteli::mcu::SetMediaPropertiesReq_CodecProperty & cprop = req.codecproperty(); 
	 		if ( ! cprop.has_fps() || ! cprop.has_bitrate() || ! cprop.has_intraperiod() || ! cprop.has_size() )
			{
			    mcu->ReleaseConferenceRef(partId.confid());
	 		    return  ReplyError( h, &partId, ErrorRsp_ErrCode_INVALID_PARAMETER,
						"SetMediaPropertiesReq[Video]: one of the following parameter "
						"is missing in the request: fps, bitrate, intraperiod or picture size.");
		    
		        }
		         
		        conf->SetVideoCodec((int) partId.partid(), 
		                        (int) ConvVideoCodecType(cprop.codec()), (int) cprop.size(),
					(int) cprop.fps(), (int) cprop.bitrate(), (int) cprop.intraperiod(),
					properties);
		        resp.set_nbpropapplied(properties.size());
		        mcu->ReleaseConferenceRef(partId.confid());
		    }
		    break;
		    
		case moteli::mcu::TEXT:
		    conf->SetTextCodec((int) partId.partid(), (int) ConvTextCodecType(req.codecproperty().codec()));
		    resp.set_nbpropapplied(properties.size());
		    mcu->ReleaseConferenceRef(partId.confid());
		    break;
		
		default:
		    mcu->ReleaseConferenceRef(partId.confid());
		    return  ReplyError( h, &partId, 
			    ErrorRsp_ErrCode_INVALID_PARAMETER,
			    "SetMediaPropertiesReq: unsupported media");
	    }
	    
	    
	    resp.set_allocated_partid( new McuObjectId(partId));
	    resp.set_mediatype( req.mediatype() );
	    return SendMessage(h, &resp);
        }
	catch(std::exception e)
	{
	    Error("-SetMediaPropertiesReq: exception caught. confId=%d, partId=%d.\n", partId.confid(), partId.partid() ); 
	    Error("-SetMediaPropertiesReq: exception msg = %s.\n", e.what() );
	    if (conf != NULL) mcu->ReleaseConferenceRef(partId.confid());
	    return ReplyError( h, &partId, ErrorRsp_ErrCode_PROCESSING_ERR,
		               e.what());

	}
    }
    else
    {
        return ReplyError( h, NULL, ErrorRsp_ErrCode_INVALID_MESSAGE,
		           "Cannot parse SetMediaPropertiesReq message");
    }

    return -2;
}


int McuRabbitHandler::onStartMediaReq( const char * payload, size_t len,  MsgHeader & h )
{
    moteli::mcu::StartMediaReq req;
    if ( req.ParseFromArray( payload, len ) )
    {
	const McuObjectId & partId = req.partid();
	MultiConf * conf = NULL;
	Properties properties;
	moteli::mcu::StartMediaResp resp;
	
	if ( partId.objtype() != McuObjectId_ObjectType_PARTICIPANT )
	{
	    return  ReplyError( h, &partId, 
			    ErrorRsp_ErrCode_INVALID_PARAMETER,
			    "StartMediaReq: partId is not a participant.");
	}
	
	if(!mcu->GetConferenceRef(partId.confid(), &conf))
		return  ReplyError( h, &partId, 
			    ErrorRsp_ErrCode_INVALID_PARAMETER,
			    "StartMediaReq: cannot find specified confId");

	if ( req.medias_size() > 0 )
	{
	    try
	    {
		resp.clear_medias();
		for (int i=0; i< req.medias_size(); i++)
		{
			const moteli::mcu::MediaDef mdef = req.medias(i);
			
			if (mdef.mediadirection() == moteli::mcu::SEND || mdef.mediadirection() == moteli::mcu::SENDRECV)
			{
			    StartSendingMedia( conf, partId.partid(), ConvMediaType(mdef.mediatype()), &mdef, &resp );
			}
			
			if (mdef.mediadirection() == moteli::mcu::RECV || mdef.mediadirection() == moteli::mcu::SENDRECV)
			{
			    int port = StartReceivingMedia( conf, partId.partid(), ConvMediaType(mdef.mediatype()), &mdef, &resp );

			    moteli::mcu::MediaStatus * mstat = resp.add_medias();
        		    mstat->set_mediatype(mdef.mediatype());
        		    mstat->set_mediadirection(moteli::mcu::RECV);
			    if (port > 0 )
			    {
				moteli::mcu::MediaStatus_MediaLocalAddr * localAddr = new moteli::mcu::MediaStatus_MediaLocalAddr();
            			localAddr->set_port(port);

				// TODO : obtain IP address automatically
            			localAddr->set_ipaddr("217.146.224.213");
            			mstat->set_allocated_localaddr(localAddr);
				mstat->set_status(true);
			    }
			    else
			    {
				mstat->set_status(false);
			    }
			}
		}
		mcu->ReleaseConferenceRef(partId.confid());
	        resp.set_allocated_partid( new McuObjectId(partId));
	        return SendMessage(h, &resp);
	    }
	    catch(std::exception e)
	    {
	        Error("-StartMediaReq: exception caught. confId=%d, partId=%d.\n", partId.confid(), partId.partid() ); 
	        Error("-StartMediaReq: exception msg = %s.\n", e.what() );
	        if (conf != NULL) mcu->ReleaseConferenceRef(partId.confid());
	        return ReplyError( h, &partId, ErrorRsp_ErrCode_PROCESSING_ERR,
		               e.what());

	    }	    
	}
	else
	{
		mcu->ReleaseConferenceRef(partId.confid());
		return  ReplyError( h, &partId, 
			    ErrorRsp_ErrCode_INVALID_PARAMETER,
			    "StartMediaReq: no media specified");
	}
    }
    else
    {
        return ReplyError( h, NULL, ErrorRsp_ErrCode_INVALID_MESSAGE,
		           "Cannot parse StartMediaReq message");
    }
    
}

inline const char* GetNameForMedia(MediaFrame::Type media)
{
	switch (media)
	{
		case MediaFrame::Audio:
			return "audio";
		case MediaFrame::Video:
			return "video";
		case MediaFrame::Text:
			return "text";
	}
	return "unknown media";
}

int McuRabbitHandler::StartSendingMedia( MultiConf *conf, int partId, int mt, const void * m, void * r)
{
    MediaFrame::Type media = (MediaFrame::Type) mt;
    const moteli::mcu::MediaDef * mdef = (const moteli::mcu::MediaDef *) m;
    moteli::mcu::StartMediaResp * resp = (moteli::mcu::StartMediaResp *) r;
 
    Log("> Start sending media %s.\n", GetNameForMedia(media) );
    if (! mdef->has_peeraddr() )
    {
        Error("-StartMediaReq: start sending media %s but no peer IP/port specified in request.\n", GetNameForMedia(media));
	throw std::runtime_error("StartMediaReq: missing peer IP address / port");
    }
    
    if ( mdef->sendingrtpmap_size() > 0 )
    {
        // Convert sendingRtpMap in internal MCU rtp map
        RTPMap map;
	moteli::mcu::MediaStatus * mstat = resp->add_medias();
	mstat->set_mediatype(mdef->mediatype());
	mstat->set_mediadirection(moteli::mcu::SEND);
        for (int i=0; i<mdef->sendingrtpmap_size(); i++ )
	{
	     const moteli::mcu::MediaDef_RtpMapEntry & entry = mdef->sendingrtpmap(i);

	     switch(media)
	     {
		case MediaFrame::Audio:
		    map[ entry.payloadtype() ] = ConvAudioCodecType( entry.codec() );
		    break;
		    
		case MediaFrame::Video:
		    map[ entry.payloadtype() ] = ConvVideoCodecType( entry.codec() );
		    break;

		case MediaFrame::Text:
		    map[ entry.payloadtype() ] = ConvTextCodecType( entry.codec() );
		    break;
	     }
	}
	int res  = conf->StartSending(partId, media, (char *) mdef->peeraddr().ipaddr().c_str(),mdef->peeraddr().port(),map);
	if (res > 0)
	{
	    mstat->set_status(true);
	}
	else
	{
	    mstat->set_status(false);
	}	

	switch(media)
	{
	    case MediaFrame::Audio:
		conf->AddSidebarParticipant(0,partId);
		break;

	    case MediaFrame::Video:
		conf->AddMosaicParticipant(0,partId);
		break;

	    default:
		break;
	}
    }
    else
    {
	Log("-StartMediaReq: RTP map for media %s is empty. Not sending any media.`\n", GetNameForMedia(media));
    }
    Log("< Start sending media %s.\n", GetNameForMedia(media) );
    return 1;
}


int McuRabbitHandler::StartReceivingMedia( MultiConf *conf, int partId, int mt, const void * m, void * r )
{
    MediaFrame::Type media = (MediaFrame::Type) mt;
    const moteli::mcu::MediaDef * mdef = (const moteli::mcu::MediaDef *) m;
    moteli::mcu::StartMediaResp * resp = (moteli::mcu::StartMediaResp *) r;
    int port = 0;
    Log("> Start receiving media %s.\n", GetNameForMedia(media) );
    if ( mdef->recevingrtpmap_size() > 0 )
    {
        // Convert sendingRtpMap in internal MCU rtp map
        RTPMap map;
	//mstat->set_mediatype(mdef->mediatype());
	//mstat->set_mediadirection(moteli::mcu::RECV);
	
        for (int i=0; i<mdef->recevingrtpmap_size(); i++ )
	{
	     const moteli::mcu::MediaDef_RtpMapEntry & entry = mdef->recevingrtpmap(i);
	     
	     switch(media)
	     {
		case MediaFrame::Audio:
		    map[ entry.payloadtype() ] = ConvAudioCodecType( entry.codec() );
		    break;
		    
		case MediaFrame::Video:
		    map[ entry.payloadtype() ] = ConvVideoCodecType( entry.codec() );
		    break;

		case MediaFrame::Text:
		    map[ entry.payloadtype() ] = ConvTextCodecType( entry.codec() );
		    break;
	     }
	}
	port = conf->StartReceiving(partId, media, map);
    }
    else
    {
	Log("-StartMediaReq: RTP map for media %s is empty. Not receiving any media.`\n", GetNameForMedia(media));
    }
    Log("< Start receiving media %s.\n", GetNameForMedia(media) );
    return port;    
}


int McuRabbitHandler::onStopMediaReq( const char * payload, size_t len,  MsgHeader & h )
{
    moteli::mcu::StopMediaReq req;
    if ( req.ParseFromArray( payload, len ) )
    {
	const McuObjectId & partId = req.partid();
	MultiConf * conf = NULL;
	Properties properties;
	moteli::mcu::StopMediaResp resp;
	
	if ( partId.objtype() != McuObjectId_ObjectType_PARTICIPANT )
	{
	    return  ReplyError( h, &partId, 
			    ErrorRsp_ErrCode_INVALID_PARAMETER,
			    "StopMediaReq: partId is not a participant.");
	}
	
	if(!mcu->GetConferenceRef(partId.confid(), &conf))
		return  ReplyError( h, &partId, 
			    ErrorRsp_ErrCode_INVALID_PARAMETER,
			    "StopMediaReq: cannot find specified confId");

	if ( req.medias_size() > 0 )
	{
	    try
	    {
		for (int i=0; i< req.medias_size(); i++)
		{
			const moteli::mcu::MediaDef mdef = req.medias(i);
			
			if (mdef.mediadirection() == moteli::mcu::SEND || mdef.mediadirection() == moteli::mcu::SENDRECV)
			{
			    StopSendingMedia( conf, partId.partid(), ConvMediaType(mdef.mediatype()), &mdef, &resp );
			}
			
			if (mdef.mediadirection() == moteli::mcu::RECV || mdef.mediadirection() == moteli::mcu::SENDRECV)
			{
			    StopReceivingMedia( conf, partId.partid(), ConvMediaType(mdef.mediatype()), &mdef, &resp );
			}
		}
		mcu->ReleaseConferenceRef(partId.confid());
	        resp.set_allocated_partid( new McuObjectId(partId));
	        return SendMessage(h, &resp);
	    }
	    catch(std::exception e)
	    {
	        Error("-StopMediaReq: exception caught. confId=%d, partId=%d.\n", partId.confid(), partId.partid() ); 
	        Error("-StopMediaReq: exception msg = %s.\n", e.what() );
	        if (conf != NULL) mcu->ReleaseConferenceRef(partId.confid());
	        return ReplyError( h, &partId, ErrorRsp_ErrCode_PROCESSING_ERR,
		               e.what());

	    }	    
	}
	else
	{
		mcu->ReleaseConferenceRef(partId.confid());
		return  ReplyError( h, &partId, 
			    ErrorRsp_ErrCode_INVALID_PARAMETER,
			    "StartMediaReq: no media specified");
	}
    }
    else
    {
        return ReplyError( h, NULL, ErrorRsp_ErrCode_INVALID_MESSAGE,
		           "Cannot parse StartMediaReq message");
    }
    
}

int McuRabbitHandler::StopSendingMedia( MultiConf *conf, int partId, int mt, const void * m, void * r)
{
    MediaFrame::Type media = (MediaFrame::Type) mt;
    const moteli::mcu::MediaDef * mdef = (const moteli::mcu::MediaDef *) m;
    moteli::mcu::StopMediaResp * resp = (moteli::mcu::StopMediaResp *) r;
    moteli::mcu::MediaStatus * mstat = resp->add_medias();
 
    Log("> Stop sending media %s.\n", GetNameForMedia(media) );
    
    int res  = conf->StopSending(partId, media);
    if (res > 0)
    {
	mstat->set_status(true);
    }
    else
    {
	mstat->set_status(false);	
    }
    return res;
}

int McuRabbitHandler::StopReceivingMedia( MultiConf *conf, int partId, int mt, const void * m, void * r)
{
    MediaFrame::Type media = (MediaFrame::Type) mt;
    const moteli::mcu::MediaDef * mdef = (const moteli::mcu::MediaDef *) m;
    moteli::mcu::StopMediaResp * resp = (moteli::mcu::StopMediaResp *) r;
    moteli::mcu::MediaStatus * mstat = resp->add_medias();
 
    Log("> Stop receivi g media %s.\n", GetNameForMedia(media) );
    
    int res  = conf->StopReceiving(partId, media);
    if (res > 0)
    {
	mstat->set_status(true);
    }
    else
    {
	mstat->set_status(false);	
    }
    return res;
}


int McuRabbitHandler::onAddToMixerReq( const char * payload, size_t len,  MsgHeader & h )
{
    moteli::mcu::AddParticipantsToMixerReq req;
    if ( req.ParseFromArray( payload, len ) )
    {
	const McuObjectId & mixerId = req.objid();
	MultiConf * conf = NULL;
	Properties properties;
	moteli::mcu::AddParticipantsToMixerResp resp;

	if (!mcu->GetConferenceRef(mixerId.confid(), &conf))
		return  ReplyError( h, &mixerId, 
			    ErrorRsp_ErrCode_INVALID_PARAMETER,
			    "onAddToMixerReq: cannot find specified confId");
	
	try
	{
		switch( mixerId.objtype() )
		{
		     case McuObjectId_ObjectType_SIDEBAR:
			if ( ! mixerId.has_sidebarid() )
			{
				if (conf != NULL) mcu->ReleaseConferenceRef(mixerId.confid());
				return ReplyError( h, &mixerId, 
						   ErrorRsp_ErrCode_INVALID_PARAMETER,
						   "onAddToMixerReq: no SIDEBAR id specified.");			
			}
		     
			for (int i=0; i< req.partids_size(); i++)
			{
				int partId = req.partids(i);
				if ( conf->AddSidebarParticipant(mixerId.sidebarid() ,partId) )
				{
				    resp.add_partids(partId);
				}
			}
			break;
			 
		     
		     case McuObjectId_ObjectType_MOSAIC:
			if ( ! mixerId.has_mosaicid() )
			{
				if (conf != NULL) mcu->ReleaseConferenceRef(mixerId.confid());
				return ReplyError( h, &mixerId, 
						   ErrorRsp_ErrCode_INVALID_PARAMETER,
						   "onAddToMixerReq: no MOSAIC id specified.");			
			}
		     
			for (int i=0; i< req.partids_size(); i++)
			{
				int partId = req.partids(i);
				if ( conf->AddMosaicParticipant(mixerId.mosaicid() ,partId) )
				{
				    resp.add_partids(partId);
				}
			}
			break;
		     
		     default:
		        if (conf != NULL) mcu->ReleaseConferenceRef(mixerId.confid());
			return ReplyError( h, &mixerId, 
					   ErrorRsp_ErrCode_INVALID_PARAMETER,
					   "onAddToMixerReq: specified object ID must be a mosaic or a sidebar");
		}
	}
	catch(std::exception & e)
	{
	        Error("-onAddToMixerReq: exception caught. confId=%d.\n", mixerId.confid(), 0 ); 
	        Error("-onAddToMixerReq: exception msg = %s.\n", e.what() );
	        if (conf != NULL) mcu->ReleaseConferenceRef(mixerId.confid());
	        return ReplyError( h, &mixerId, ErrorRsp_ErrCode_PROCESSING_ERR,
		               e.what());

	}	    
	
	resp.set_allocated_objid(new McuObjectId(mixerId));
	if (conf != NULL) mcu->ReleaseConferenceRef(mixerId.confid());
	return SendMessage(h, &resp);
    }
    else
    {
        return ReplyError( h, NULL, ErrorRsp_ErrCode_INVALID_MESSAGE,
                           "Cannot parse StartMediaReq message");
    }

}
