#include "WSEndpoint.h"
#include <stdexcept>

static BYTE BOMUTF8[]			= {0xEF,0xBB,0xBF};

int WSEndpoint::wsPort = 0;
char* WSEndpoint::wsHost = NULL;

WSEndpoint::WSEndpoint(MediaFrame::Type type) : Port(type, MediaFrame::WS)
{
    msgType = WebSocket::Binary;
    joined = NULL;
	_ws = NULL;
	RedCodec = new RedundentCodec();
	
    useRed = false;
    pseudoSeqNum = 0;
    switch (type)
    {
        case MediaFrame::Text:
		{
			media = new TextFrame();
			break;
		}
		default:
			throw new std::logic_error("Not supported media type\n" ); // , MediaFrame::TypeToString( type )
			break;
    }
}

void WSEndpoint::onOpen(WebSocket *ws)
{
    if ( _ws == NULL )
    {
		gettimeofday(&clock,NULL);
		_ws = ws;
    }
    else
    {
    	_ws->Close();
		_ws = ws;
    }
}

void WSEndpoint::onError(WebSocket *ws)
{
	Error("WSEndpoint: websocket connection reported an error.\n");
}


void WSEndpoint::onMessageStart(WebSocket *ws, WebSocket::MessageType type, const DWORD length)
{
	//Reset frame
    media->SetLength(0);
    msgType = type;
    media->Alloc(length); 
   // Todo : if alloc fail, 
}

void WSEndpoint::onMessageData(WebSocket *ws,const BYTE* data, const DWORD size)
{
    media->AppendMedia( (BYTE*)data, size );
}

void WSEndpoint::onMessageEnd(WebSocket *ws)
{
   switch( media->GetType() )
   {
        case MediaFrame::Text:
			if (useRed)
			{
				media->SetTimestamp(getDifTime(&clock)/1000);
				RTPRedundantPacket * packet = RedCodec->Encode(media, payloadType);
                if (packet)
                {
				    packet->SetSeqNum(pseudoSeqNum);
				    packet->SetSeqCycles(pseudoSeqCycle);
				    Multiplex(*packet);
                    delete packet;
                }
                else
                {
                    Error("Media RED [%u] packet encoding fail.\n", payloadType);
                }
			}
			else
			{
				RTPPacket packet(MediaFrame::Text, TextCodec::T140);
				packet.SetTimestamp(getDifTime(&clock)/1000);
				packet.SetPayload(media->GetData(),media->GetLength());		
				packet.SetSeqNum(pseudoSeqNum);
				packet.SetSeqCycles(pseudoSeqCycle);
				Multiplex(packet);				
			}	
            if( pseudoSeqNum == 0xFFFF )
            {
                pseudoSeqCycle++;
            }

			pseudoSeqNum++;
		    break;
	    
        default:
            break;
   }
}

void WSEndpoint::onRTPPacket( RTPPacket &packet )
{
    if( _ws != NULL )
    {
        if( packet.GetMedia() == media->GetType() )
        {
            switch( packet.GetMedia() )
            {
                case MediaFrame::Text:
                {
                    //Check the type of data
                    if( packet.GetCodec() == TextCodec::T140RED )
                    {
                        //Get redundant packet
                        RTPRedundantPacket *red = (RTPRedundantPacket *)&packet;

                        RedCodec->Decode( red, this );
                    }
                    else if( packet.GetCodec() == TextCodec::T140 )
                    {
                        //Create frame
                        TextFrame frame( packet.GetTimestamp(), packet.GetMediaData(), packet.GetMediaLength() );
                        //Send it
                        SendFrame( frame );
                    }
                    else
                    {
                        Error( "Text codec %d: not supported.\n", packet.GetCodec() );
                    }
                    break;
                }

                default:
                    Error( "WSEndpoint does not support media %s.\n", MediaFrame::TypeToString( packet.GetMedia() ) );
                    break;
            }
        }
        else
        {
            Error( "WSEndpoint is associated with media %s. Cannot deliver %s packet.\n",
                MediaFrame::TypeToString( media->GetType() ),
                MediaFrame::TypeToString( packet.GetMedia() ) );
        }
    }
    else
    {
        Debug( "WSEndpoint: no Websocket is associated yet.\n" );
    }
}

void WSEndpoint::onClose(WebSocket *ws)
{
    if ( _ws == ws )
    {
        Log("WSEndpoint: connection associated with endpoint is closing.\n");
		_ws = 0;
	
		// Signal the interription as per
		SendReplacementChar(false);
    }
}

void WSEndpoint::SendReplacementChar(bool toWsSide)
{
    if (toWsSide)
    {
        if (_ws != NULL) 
		{
		}
    }
}

void WSEndpoint::SetLocalPort(int port)
{
	//return 1;
	wsPort = port;
}

void WSEndpoint::SetLocalHost(char* host)
{
	//return 1;
	wsHost = host;
}

int WSEndpoint::GetLocalPort()
{
	//return 1;
	return wsPort;
}

char* WSEndpoint::GetLocalHost()
{
	//return 1;
	return wsHost;
}

WSEndpoint::~WSEndpoint()
{
    End();
}


int WSEndpoint::End()
{
    if ( _ws != NULL )
    {
        _ws->Close();
		_ws = NULL;
    }
	
	RedCodec = NULL;
}


int WSEndpoint::SendFrame(TextFrame &frame)
{
	std::string msg( (const char*) frame.GetData(), frame.GetLength());
	if ( frame.GetLength() == 3 && memcmp(frame.GetData(), BOMUTF8, 3) == 0)
	{
		TextFrame bom(getDifTime(&clock)/1000, BOMUTF8,3);
		if (useRed)
		{
			RTPRedundantPacket *packet = RedCodec->Encode( &bom, payloadType);
            if (packet)
            {
			    Multiplex(*packet);
			    delete packet;
            }
            else
            {
                Error("Media RED [%u] packet encoding BOM fail.\n", payloadType);
            }
		}
		else
		{
			RTPPacket packet(MediaFrame::Text, TextCodec::T140);
			packet.SetTimestamp(getDifTime(&clock)/1000);
			packet.SetPayload(BOMUTF8,3);
			Multiplex(packet);			
		}
		Debug("BOM ping pong.\n");
	}
	
    if (_ws) _ws->SendMessage( msg );
}

