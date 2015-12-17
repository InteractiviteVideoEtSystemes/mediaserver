#include "xmlstreaminghandler.h"

class ParticipantEvent: public XmlEvent
{
public:

	enum ParticipantEventType
	{
		PACKET_LOSS = 1, /* many packet loss */
		VIDEO_DECODING_ERROR = 2, /* fail to decode video */
		MEDIA_TIMEOUT = 3, /* no media received since 20s typically */
		MEDIA_TRANSPORT_CLOSED = 4 /* RTMP socket closure or RTCP Goodbye */
		DTMF_RECEIVED, /* future use if participant can interact withd DTMFs*/
		FLOOR_CONTROL, /* future use when supporting BFCP */
		CAMERA_CONTROL /* future use for camera control */
	}

        ParticipantEvent(ParticipantEventType p_type, int p_confId, int p_partId)
        {
		evtType = p_type;
		confId = p_confId;
		partId = p_partId;
        }

        virtual xmlrpc_value* GetXmlValue(xmlrpc_env *env)
        {
                return xmlrpc_build_value(env,"(siii)",
			"ParticipantEvent", (int) evtType, confId, partId );
        }
private:
	ParticipantEventType evtType;
	int confId;
	int partId;
};

class McuEvent : XmlEvent
{
publc:
	enum McuEventType
	{
		MCU_RESTARTED = 0, /* notify that MCU has restarted */
		MCU_STOPPED, /* MCU has stopped normally */
		CONF_EXPIRED 
	}

        virtual xmlrpc_value* GetXmlValue(xmlrpc_env *env)
        {
		if ( evtType < CONF_EXPIRED )
		{
                    return xmlrpc_build_value(env,"(si)",
			"McuEvent", (int) evtType );
		}
		else
		{
                    return xmlrpc_build_value(env,"(sii)",
			"McuEvent", (int) evtType, confId );
		}
        }
private:
	McuEventType evtType;
	int confId;
};
