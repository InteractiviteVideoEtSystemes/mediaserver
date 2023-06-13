/*
 * File:   Endpoint.cpp
 * Author: Sergio
 *
 * Created on 7 de septiembre de 2011, 0:59
 */
#include "log.h"
#include "Endpoint.h"
#include "RTPEndpoint.h"
#include "WSEndpoint.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

Endpoint::Endpoint( std::wstring n, bool audioSupported, bool videoSupported, bool textSupported ) : eventSource( n )
{
    //Store name
    name = n;
    //Nullify
    for( int i = 0; i < 3; i++ )
    {
        ports[i] = ports2[i] = NULL;
    }
    //If audio
    if( audioSupported )
    {
        //Create endpoint
        ports[MediaFrame::Audio] = new RTPEndpoint( MediaFrame::Audio );
    }
    //If video
    if( videoSupported )
    {
        //Create endpoint
        ports[MediaFrame::Video] = new RTPEndpoint( MediaFrame::Video );
        ports2[MediaFrame::Video] = new RTPEndpoint( MediaFrame::Video, MediaFrame::VIDEO_SLIDES );
    }
    //If video
    if( textSupported )
    {
        ports[MediaFrame::Text] = new RTPEndpoint( MediaFrame::Text );
    }
    estimator.SetEventSource( &eventSource );
    estimator2.SetEventSource( &eventSource );
}

Endpoint::~Endpoint()
{
    for( int i = 0; i < 3; i++ )
    {
        if( ports[i] ) delete ports[i];
        if( ports2[i] ) delete ports2[i];
    }
}

//Methods
int Endpoint::Init()
{
    for( int i = 0; i < 3; i++ )
    {
        if( ports[i] )
        {
            ports[i]->Init();
            if( i == MediaFrame::Video && ports[i]->GetTransport() == MediaFrame::RTP )
            {
                RTPEndpoint *rtp = (RTPEndpoint *)ports[i];
                rtp->SetRemoteRateEstimator( &estimator );
            }
        }

        if( ports2[i] )
        {
            ports2[i]->Init();
            if( i == MediaFrame::Video && ports2[i]->GetTransport() == MediaFrame::RTP )
            {
                RTPEndpoint *rtp = (RTPEndpoint *)ports2[i];
                rtp->SetRemoteRateEstimator( &estimator2 );
            }
        }
    }
}

int Endpoint::End()
{
    Log( ">End endpoint\n" );

    for( int i = 0; i < 3; i++ )
    {
        if( ports[i] )
        {
            ports[i]->End();
        }

        if( ports2[i] )
        {
            ports2[i]->End();
        }
    }

    Log( "<End endpoint\n" );
}

RTPEndpoint *Endpoint::GetRTPEndpoint( MediaFrame::Type media, MediaFrame::MediaRole role )
{
    Port *p = GetPort( media, role );

    if( p != NULL && p->GetTransport() == MediaFrame::RTP )
    {
        return (RTPEndpoint *)p;
    }
    else
        return NULL;
}

int Endpoint::StartSending( MediaFrame::Type media, char *sendIp, int sendPort, RTPMap &rtpMap, MediaFrame::MediaRole role )
{
    //Get enpoint
    RTPEndpoint *rtp = GetRTPEndpoint( media, role );
    Port *p = GetPort( media, role );

    //Check 
    if( !p )
        //Init it
        return Error( "No media supported" );

    //Stop sending for a while
    p->StopSending();

    //Set remote endpoint
    if( rtp != NULL )
    {
        RTPEndpoint *rtp2 = (RTPEndpoint *)p;
        if( !rtp->SetRemotePort( sendIp, sendPort ) )
            //Error
            return Error( "Error SetRemotePort\n" );

            //Set sending map
        rtp->SetSendingRTPMap( rtpMap );
    }

    //And send
    return p->StartSending();
}

int Endpoint::StopSending( MediaFrame::Type media, MediaFrame::MediaRole role )
{
    //Get rtp enpoint for media
    Port *p = GetPort( media, role );

    //Check
    if( !p )
        //Init it
        return Error( "No media supported" );

    //Stop sending for a while
    return p->StopSending();
}

int Endpoint::StartReceiving( MediaFrame::Type media, RTPMap &rtpMap, MediaFrame::MediaRole role )
{
    Log( "-StartReceiving endpoint [name:%ls, media:%s]\n", name.c_str(), MediaFrame::TypeToString( media ) );

    //Get rtp enpoint for media
    Port *p = GetPort( media, role );

    switch( p->GetTransport() )
    {
        case MediaFrame::RTP:
        {
            RTPEndpoint *rtp = GetRTPEndpoint( media, role );
            //Set map for RTP
            if( rtp )
                rtp->SetReceivingRTPMap( rtpMap );
            break;
        }

        case MediaFrame::WS:
        {
            WSEndpoint *wsp = (WSEndpoint *)(p);
            Log( "-StartReceiving WS endpoint\n" );
            for( RTPMap::iterator it = rtpMap.begin(); it != rtpMap.end(); ++it )
            {
                //Is it our codec
                if( it->second == TextCodec::T140RED )
                {
                    wsp->SetUseRed( true );
                }

                //Is it our codec
                if( it->second == TextCodec::T140 )
                {
                    //Set it
                    wsp->SetPrimaryPayloadType( it->first );
                    //and we are done
                    continue;
                }
            }

            break;
        }

        default:
            Error( "Protocol not supported\n" );
            return -1;
    }
    //Check
    if( !p )
        //Init it
        return Error( "No media supported\n" );

    //Start
    if( !p->StartReceiving() )
        //Exit
        return Error( "Error starting receiving media\n" );

    //Return port
    return p->GetLocalMediaPort();
}

int Endpoint::StopReceiving( MediaFrame::Type media, MediaFrame::MediaRole role )
{
    //Get rtp enpoint for media
    Port *p = GetPort( media, role );

    //Check
    if( !p )
        //Init it
        return Error( "No media supported\n" );
    //Start
    return p->StopReceiving();
}

//Attach
int Endpoint::Attach( MediaFrame::Type media, MediaFrame::MediaRole role, Joinable *join )
{
    Log( "-Endpoint attaching [media:%s]\n", MediaFrame::TypeToString( media ) );

    //Get rtp enpoint for media
    Port *p = GetPort( media, role );

    //Check
    if( !p )
        //Init it
        return Error( "No media supported\n" );

    p->Attach( join );

    return 1;
}

int Endpoint::Detach( MediaFrame::Type media, MediaFrame::MediaRole role )
{
    Log( "-Endpoint detaching [media:%s]\n", MediaFrame::TypeToString( media ) );

    Port *p = GetPort( media, role );

    //Check
    if( !p )
        //Init it
        return Error( "No media supported\n" );

    return p->Detach();
}

Joinable *Endpoint::GetJoinable( MediaFrame::Type media, MediaFrame::MediaRole role )
{
    Log( "<Endpoint GetJoinable [media:%s]\n", MediaFrame::TypeToString( media ) );

    Port *p = GetPort( media, role );

    //Check
    if( !p )
    {
        //Init it
        Error( "This media or role is not supported\n" );
        return NULL;
    }

    return p;

}


int Endpoint::RequestUpdate( MediaFrame::Type media, MediaFrame::MediaRole role )
{
    RTPEndpoint *rtp = GetRTPEndpoint( media, role );

    //Check
    if( rtp ) return rtp->RequestUpdate();

    return Error( "Unknown media [%s]\n", MediaFrame::TypeToString(media) );
}

int Endpoint::SetLocalCryptoSDES( MediaFrame::Type media, const char *suite, const char *key, MediaFrame::MediaRole role )
{
    RTPEndpoint *rtp = GetRTPEndpoint( media, role );

    //Check
    if( rtp ) return rtp->SetLocalCryptoSDES( suite, key );

    return Error( "Unknown media [%s]\n", MediaFrame::TypeToString( media ) );
}

int Endpoint::SetRemoteCryptoSDES( MediaFrame::Type media, const char *suite, const char *key, MediaFrame::MediaRole role )
{
    RTPEndpoint *rtp = GetRTPEndpoint( media, role );

    if( rtp ) return rtp->SetRemoteCryptoSDES( suite, key );

    return Error( "Unknown media [%s]\n", MediaFrame::TypeToString( media ) );
}

int Endpoint::SetRemoteCryptoDTLS( MediaFrame::Type media, const char *setup, const char *hash, const char *fingerprint, MediaFrame::MediaRole role )
{
    RTPEndpoint *rtp = GetRTPEndpoint( media, role );

    if( rtp ) return rtp->SetRemoteCryptoDTLS( setup, hash, fingerprint );

    return Error( "Unknown media [%s]\n", MediaFrame::TypeToString( media ) );
    //OK
    return 1;
}

int Endpoint::SetLocalSTUNCredentials( MediaFrame::Type media, const char *username, const char *pwd, MediaFrame::MediaRole role )
{
    RTPEndpoint *rtp = GetRTPEndpoint( media, role );

    if( rtp ) return rtp->SetLocalSTUNCredentials( username, pwd );

    return Error( "Unknown media [%s]\n", MediaFrame::TypeToString( media ) );
}

int Endpoint::SetRTPProperties( MediaFrame::Type media, const Properties &properties, MediaFrame::MediaRole role )
{
    RTPEndpoint *rtp = GetRTPEndpoint( media, role );

    if( rtp ) return rtp->SetProperties( properties );

    return Error( "Unknown media [%s] - cannot set rtp properties\n", MediaFrame::TypeToString( media ) );
}

int Endpoint::SetRTPTsTransparency( MediaFrame::Type media, bool transparency, MediaFrame::MediaRole role )
{
    RTPEndpoint *rtp = GetRTPEndpoint( media, role );

    if( rtp )
    {
        rtp->SetTsTransparency( transparency );
        return 1;
    }

    return Error( "Unknown media [%s] - cannot set transparency\n", MediaFrame::TypeToString( media ) );
}

int Endpoint::SetRemoteSTUNCredentials( MediaFrame::Type media, const char *username, const char *pwd, MediaFrame::MediaRole role )
{
    RTPEndpoint *rtp = GetRTPEndpoint( media, role );

    if( rtp ) return rtp->SetRemoteSTUNCredentials( username, pwd );

    return Error( "Unknown media [%s] - cannot set stun credentials\n", MediaFrame::TypeToString( media ) );
}

int Endpoint::onNewMediaConnection( MediaFrame::Type media, MediaFrame::MediaRole role,
    MediaFrame::MediaProtocol transp, WebSocket *ws )
{
    Port **p;

    if( role == MediaFrame::VIDEO_MAIN )
    {
        p = &(ports[media]);
    }
    else if( media == MediaFrame::Video && role != MediaFrame::VIDEO_MAIN )
    {
        p = &(ports2[media]);
    }
    else
    {
        return Error( "Invalid media %s / role %d parameters\n", MediaFrame::TypeToString( media ), role );
    }

    if( (*p) != NULL && (*p)->GetTransport() != MediaFrame::WS )
    {
        // Previous port was not a web socket
        Log( "Endpoint: replacing port transport=%d for %s by WebSocket\n",
            (*p)->GetTransport(), MediaFrame::TypeToString( media ) );

        (*p)->End();
        delete (*p);

        (*p) = new WSEndpoint( media );
    }
    else
    {
        // Previous port was already a using websocket
        Log( "Endpoint: Accepting WebSocket connection for media %s\n", MediaFrame::TypeToString( media ) );
    }

    ws->Accept( (WSEndpoint *)*p );
    return 1;
}

int Endpoint::Port::Attach( Joinable *join )
{
    if( join == joined )
    {
        return 1;
    }

    //Store new one
    joined = join;
     //If it is not null
    if( joined )
        //Join to the new one
        joined->AddListener( (RTPEndpoint *)this );

    //OK
    return 1;
}

int Endpoint::Port::Detach()
{
    //Detach if joined
    if( joined )
        //Remove ourself as listeners
        joined->RemoveListener( (RTPEndpoint *)this );
    //Not joined anymore
    joined = NULL;
    return 1;
}

int Endpoint::Port::GetLocalMediaPort()
{
    switch( proto )
    {
        case MediaFrame::RTP:
        {
            RTPEndpoint *rtp = (RTPEndpoint *)(this);
            return rtp->GetLocalPort();
        }

        case MediaFrame::WS:
        {
            WSEndpoint *wsp = (WSEndpoint *)(this);
            return wsp->GetLocalPort();
        }

        default:
            Error( "Protocol %s not supported on media port\n", MediaFrame::ProtocolToString( proto ) );
            return -1;
    }
}

char *Endpoint::Port::GetLocalMediaHost()
{
    switch( proto )
    {
        /*
        case MediaFrame::RTP:
        {
            RTPEndpoint *rtp = (RTPEndpoint *)(this);
            //return rtp->GetLocalHost();
            return "172.21.100.19";
        }
        */

        case MediaFrame::WS:
        {
            WSEndpoint *wsp = (WSEndpoint *)(this);
            return wsp->GetLocalHost();
        }

        default:
            Error( "Protocol %s not supported on media host\n", MediaFrame::ProtocolToString( proto ) );
            return NULL;
    }
}


int Endpoint::ConfigureMediaConnection( MediaFrame::Type media, MediaFrame::MediaRole role,
    MediaFrame::MediaProtocol proto, const char *expectedPayload )
{
    Port *p = GetPort( media, role );

    if( p != NULL )
    {
        if( p->GetTransport() != proto )
        {
            Port *p2;

            switch( proto )
            {
                case MediaFrame::WS:
                    Log( "Recreating WSEndpoint for media %s\n", MediaFrame::TypeToString( media ) );
                    p2 = new WSEndpoint( media );
                    break;

                case MediaFrame::RTP:
                    Log( "Recreating RTPEndpoint for media %s\n", MediaFrame::TypeToString( media ) );
                    p2 = new RTPEndpoint( media );
                    break;

                default:
                    return Error( "Transport %s not supported.\n", MediaFrame::ProtocolToString( proto ) );
            }

            if( role == MediaFrame::VIDEO_MAIN )
            {
                p2->SwitchJoin( p );
                delete p;
                ports[media] = p2;
            }
            else if( media == MediaFrame::Video && role != MediaFrame::VIDEO_MAIN )
            {
                p2->SwitchJoin( p );
                delete p;
                ports2[MediaFrame::Video] = p2;
            }
            else
            {
                delete p2;
                return Error( "Invalid media=%d, role=%d.\n", media, role );
            }

            return 1;
        }
        else
        {
            // Already using the right protool
            Log( "Already using the right protocol %s\n", MediaFrame::ProtocolToString( proto ) );
            return 1;
        }
    }
    else
    {
        return Error( "Invalid media / role.\n" );
    }
}


char *Endpoint::GetMediaCandidates( MediaFrame::MediaProtocol protocol, MediaFrame::Type media )
{

    char hostname[HOST_NAME_MAX];
    //char host[80];
    char *host;
    char url[50];
    bool addrfound = false;

    if( gethostname( hostname, sizeof hostname ) == 0 )
    {
        //puts(hostname);

        if( hostname )
        {
            struct hostent *remoteHost = gethostbyname( hostname );

            if( remoteHost )
            {
                struct in_addr addr;
                int i = 0;

                if( remoteHost->h_addrtype == AF_INET )
                {
                    while( remoteHost->h_addr_list[i] != 0 )
                    {
                        addr.s_addr = *(u_long *)remoteHost->h_addr_list[i++];
                        //inet_ntoa_r( addr, host, sizeof(host) );
                        host = inet_ntoa( addr );
                        if( strcmp( host, "127.0.0.1" ) != 0 )
                        {
                            Log( "\tIPv4 Address #%d: %s\n", i, inet_ntoa( addr ) );
                            addrfound = true;
                            break;
                        }
                    }
                }
            }
        }
    }

    if( addrfound )
    {
        int port = 0;
        char *wshost = NULL;
        Port *p = GetPort( media );

        if( p == NULL )
        {
            Error( "No such media %s\n", MediaFrame::TypeToString( media ) );
            return NULL;
        }

        if( p->GetTransport() != protocol )
        {
            Error( "Media is configured with protocol %s. Cannot get media candidate for protocol %s.\n"
                , MediaFrame::ProtocolToString( p->GetTransport() )
                , MediaFrame::ProtocolToString( protocol ) 
                );
            return NULL;
        }

        port = p->GetLocalMediaPort();
        wshost = p->GetLocalMediaHost();

        if( port == -1 )
            return NULL;

        if( wshost )
            host = wshost;

        if( port > 0 )
        {
            sprintf( url, "%s://%s:%d", MediaFrame::ProtocolToString( protocol ), host, port );
        }
        else
        {
            sprintf( url, "%s://%s", MediaFrame::ProtocolToString( protocol ), host );
        }
        Log( "URL = %s\n", url );
        return strdup( url );
    }
    else
    {
        Error( "No address found. \n" );
        return NULL;
    }
}


int Endpoint::Port::SwitchJoin( Port *oldPort )
{
    if( oldPort != NULL && oldPort->joined != NULL )
    {
        Joinable *oldJoined = oldPort->joined;
        oldPort->Detach();
        Attach( oldJoined );
    }
}


Endpoint::Port::~Port()
{
    Detach();
}


int Endpoint::SetEventContextId( MediaFrame::Type media, MediaFrame::MediaRole role, int ctxId )
{
    Port *p = GetPort( media, role );
    if( p != NULL )
        p->SetEventContextId( ctxId );
}

int  Endpoint::SetEventHandler( MediaFrame::Type media, MediaFrame::MediaRole role, int sessionId, JSR309Manager *jsrManager )
{
    Port *p = GetPort( media, role );
    if( p != NULL )
        p->SetEventHandler( sessionId, jsrManager );
}


const Endpoint::Statistics *Endpoint::GetStatistics()
{
    stats.clear();

    for( int i = 0; i < 3; i++ )
    {
        MediaFrame::Type m = (MediaFrame::Type)i;

        Port *p = GetPort( m, MediaFrame::VIDEO_MAIN );

        if( p != NULL && p->GetTransport() == MediaFrame::RTP )
        {
            MediaStatistics statsport;
            std::string mediaName( MediaFrame::TypeToString( m ) );
            RTPEndpoint *rtp = (RTPEndpoint *)p;
            if( rtp->GetStatistics( 0, statsport ) )

                stats[mediaName] = statsport;
        }
    }
    return &stats;
}
