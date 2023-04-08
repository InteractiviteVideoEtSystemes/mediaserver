#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/poll.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <srtp/srtp.h>
#include <time.h>
#include "log.h"
#include "tools.h"
#include "codecs.h"
#include "rtp.h"
#include "rtpsession.h"
#include "stunmessage.h"
#include <libavutil/base64.h>
#include <openssl/ossl_typ.h>

BYTE rtpEmpty[] = { 0x80, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

//srtp library initializers
class SRTPLib
{
public:
    SRTPLib() { srtp_init(); }
};
SRTPLib srtp;

DWORD RTPSession::minLocalPort = 49152;
DWORD RTPSession::maxLocalPort = 65535;

bool RTPSession::SetPortRange( int minPort, int maxPort )
{
    // mitPort should be even
    if( minPort % 2 )
        minPort++;

    //Check port range is possitive
    if( maxPort < minPort )
        //Error
        return Error( "-RTPSession port range invalid [%d,%d]\n", minPort, maxPort );

    //check min range ports
    if( maxPort - minPort < 50 )
    {
        //Error
        Error( "-RTPSession port range too short %d, should be at least 50\n", maxPort - minPort );
        //Correct
        maxPort = minPort + 50;
    }

    //check min range
    if( minPort < 1024 )
    {
        //Error
        Error( "-RTPSession min rtp port is inside privileged range, increasing it\n" );
        //Correct it
        minPort = 1024;
    }

    //Check max port
    if( maxPort > 65535 )
    {
        //Error
        Error( "-RTPSession max rtp port is too high, decreasing it\n" );
        //Correc it
        maxPort = 65535;
    }

    //Set range
    minLocalPort = minPort;
    maxLocalPort = maxPort;

    //Log
    Log( "-RTPSession configured RTP/RTCP ports range [%d,%d]\n", minLocalPort, maxLocalPort );

    //OK
    return true;
}

unsigned char RandomByte( unsigned int seed=0 )
{
    if( seed )
    {
        srand( seed );
    }
    return ((unsigned char)(int)((256.0 * (double)rand()) / ((double)RAND_MAX + 1.0)));
}

unsigned int RandomUInt32( unsigned int seed=0 )
{
    unsigned char byte;
    unsigned int retval;

    retval = 0;
    byte = RandomByte( seed );
    retval |= ((unsigned int)byte);
    byte = RandomByte();
    retval |= (((unsigned int)byte) << 8);
    byte = RandomByte();
    retval |= (((unsigned int)byte) << 16);
    byte = RandomByte();
    retval |= (((unsigned int)byte) << 24);

    return retval;
}

/*************************
* RTPSession
* 	Constructro
**************************/
RTPSession::RTPSession( MediaFrame::Type media, Listener *listener, MediaFrame::MediaRole role )
    : dtls( *this )
{
    //Store listener
    this->listener = listener;
    //And media
    this->media = media;
    this->role = role;
    //Init values
    sendType = -1;
    simSocket = FD_INVALID;
    simRtcpSocket = FD_INVALID;
    simPort = 0;
    simRtcpPort = 0;
    sendSeq = 0;
    sendTime = RandomUInt32();
    sendLastTime = sendTime;
    sendSSRC = RandomUInt32();
    lastSendSSRC = 0;
    sendSR = 0;
    recSR = 0;
    sendCycles = 0;

    recIP = INADDR_ANY;
    recPort = 0;
    firReqNum = 0;
    requestFPU = false;
    pendingTMBR = false;
    pendingTMBBitrate = 0;
    //Not muxing
    muxRTCP = false;
    //Default cname
    cname = strdup( "default@localhost" );
    //Empty types by defauilt
    rtpMapIn = NULL;
    rtpMapOut = NULL;
    //statistics
    totalSendBytes = 0;
    numSendPackets = 0;
    //No reports
    setZeroTime( &lastSR );
    setZeroTime( &lastReceivedSR );
    rtt = 0;
    //No cripto
    encript = false;
    decript = false;
    sendSRTPSession = NULL;
    recvSRTPSession = NULL;
    recvSRTPSession_secondary = NULL;
    recvSRTPSessionRTX = NULL;
    recvSRTPSessionRTX_secondary = NULL;
    sendKey = NULL;
    recvKey = NULL;
    //No ice
    iceLocalUsername = NULL;
    iceLocalPwd = NULL;
    iceRemoteUsername = NULL;
    iceRemotePwd = NULL;
    //NO FEC
    useFEC = false;
    useNACK = false;
    useAbsTime = false;
    isNACKEnabled = false;
    //Fill with 0
    memset( sendPacket, 0, MTU + SRTP_MAX_TRAILER_LEN );
    //Preparamos las direcciones de envio
    memset( &sendAddr, 0, sizeof( struct sockaddr_in ) );
    memset( &sendRtcpAddr, 0, sizeof( struct sockaddr_in ) );
    //No thread
    setZeroThread( &thread );
    running = false;
    //No stimator
    remoteRateEstimator = NULL;
    sendBitrateFeedback = false; // test

    //Set family
    sendAddr.sin_family = AF_INET;
    sendRtcpAddr.sin_family = AF_INET;

    defaultStream = NULL;

    useOriSeqNum = false;
    useOriTS = false;
    useExtFIR = false;
    useRtcpFIR = true;
}

/*************************
* ~RTPSession
* 	Destructor
**************************/
RTPSession::~RTPSession()
{
    End();
    //Check listener
    if( remoteRateEstimator && sendBitrateFeedback )
        //Add as listener
        remoteRateEstimator->SetListener( NULL );
    if( rtpMapIn )
        delete(rtpMapIn);
    if( rtpMapOut )
        delete(rtpMapOut);
    //Delete packets
    for( Streams::iterator it = streams.begin(); it != streams.end(); it++ )
    {
        if( it->second )
            it->second->Clear();
    }
    //Clean mem
    if( iceLocalUsername )
        free( iceLocalUsername );
    if( iceLocalPwd )
        free( iceLocalPwd );
    if( iceRemoteUsername )
        free( iceRemoteUsername );
    if( iceRemotePwd )
        free( iceRemotePwd );
    //If secure
    if( sendSRTPSession )
        //Dealoacate
        srtp_dealloc( sendSRTPSession );
    //If secure
    if( recvSRTPSession )
        //Dealoacate
        srtp_dealloc( recvSRTPSession );
    if( recvSRTPSession_secondary )
        //Dealoacate
        srtp_dealloc( recvSRTPSession_secondary );
    //If RTX
    if( recvSRTPSessionRTX )
        //Dealoacate
        srtp_dealloc( recvSRTPSessionRTX );
    if( recvSRTPSessionRTX_secondary )
        //Dealoacate
        srtp_dealloc( recvSRTPSessionRTX_secondary );
    if( sendKey )
        free( sendKey );
    if( recvKey )
        free( recvKey );
    if( cname )
        free( cname );
    //Delete rtx packets
}

void RTPSession::SetSendingRTPMap( RTPMap &map )
{
    //If we already have one
    if( rtpMapOut )
        //Delete it
        delete(rtpMapOut);
    //Clone it
    rtpMapOut = new RTPMap( map );
}

int RTPSession::SetLocalCryptoSDES( const char *suite, const BYTE *key, const DWORD len )
{
    err_status_t err;
    srtp_policy_t policy;

    Log( "-Set local RTP SDES [key:%s,suite:%s]\n", key, suite );

    //empty policy
    memset( &policy, 0, sizeof( srtp_policy_t ) );

    //Get cypher
    if( strcmp( suite, "AES_CM_128_HMAC_SHA1_80" ) == 0 )
    {
        Log( "RTPSession::SetLocalCryptoSDES() | suite: AES_CM_128_HMAC_SHA1_80\n" );
        crypto_policy_set_aes_cm_128_hmac_sha1_80( &policy.rtp );
        crypto_policy_set_aes_cm_128_hmac_sha1_80( &policy.rtcp );
    }
    else if( strcmp( suite, "AES_CM_128_HMAC_SHA1_32" ) == 0 )
    {
        Log( "RTPSession::SetLocalCryptoSDES() | suite: AES_CM_128_HMAC_SHA1_32\n" );
        crypto_policy_set_aes_cm_128_hmac_sha1_32( &policy.rtp );
        crypto_policy_set_aes_cm_128_hmac_sha1_80( &policy.rtcp );
    }
    else if( strcmp( suite, "AES_CM_128_NULL_AUTH" ) == 0 )
    {
        Log( "RTPSession::SetLocalCryptoSDES() | suite: AES_CM_128_NULL_AUTH\n" );
        crypto_policy_set_aes_cm_128_null_auth( &policy.rtp );
        crypto_policy_set_aes_cm_128_null_auth( &policy.rtcp );
    }
    else if( strcmp( suite, "NULL_CIPHER_HMAC_SHA1_80" ) == 0 )
    {
        Log( "RTPSession::SetLocalCryptoSDES() | suite: NULL_CIPHER_HMAC_SHA1_80\n" );
        crypto_policy_set_null_cipher_hmac_sha1_80( &policy.rtp );
        crypto_policy_set_null_cipher_hmac_sha1_80( &policy.rtcp );
    }
    else
    {
        return Error( "Unknown cipher suite\n" );
    }

    //Check sizes
    if( len != policy.rtp.cipher_key_len )
        //Error
        return Error( "Key size (%d) doesn't match the selected srtp profile (required %d)\n", len, policy.rtp.cipher_key_len );

    //Set polciy values
    policy.ssrc.type = ssrc_any_outbound;
    policy.ssrc.value = 0;
    //policy.allow_repeat_tx  = 1; <--- Not all srtp libs containts it
    policy.key = (BYTE *)key;
    policy.next = NULL;
    srtp_t session;
    err = srtp_create( &session, &policy );
    //Check error
    if( err != err_status_ok )
        //Error
        return Error( "Failed to create srtp session (%d)\n", err );

    //Set send SSRTP sesion
    sendSRTPSession = session;

    //Request an intra to start clean
    if( listener )
        //Request a I frame
        listener->onFPURequested( this );

    //Evrything ok
    return 1;
}

int RTPSession::SetLocalCryptoSDES( const char *suite, const char *key64 )
    //Log
{
    Log( "-SetLocalCryptoSDES [key:%s,suite:%s]\n", key64, suite );

    //encript
    encript = true;

    //Get lenght
    WORD len64 = strlen( key64 );
    //Allocate memory for the key
    BYTE sendKey[len64];
    //Decode
    WORD len = av_base64_decode( sendKey, key64, len64 );

    //Set it
    return SetLocalCryptoSDES( suite, sendKey, len );
}

int RTPSession::SetProperties( const Properties &properties )
{
    mutex.lock();
    //Clean txtension map
    extMap.clear();
    //For each property
    for( Properties::const_iterator it = properties.begin(); it != properties.end(); ++it )
    {
        Log( "Setting RTP property [%s:%s] on %s %s stream %p\n", it->first.c_str(), it->second.c_str(),
            MediaFrame::RoleToString( role ), MediaFrame::TypeToString( media ), this );

        //Check
        if( it->first.compare( "rtcp-mux" ) == 0 )
        {
            //Set rtcp muxing
            muxRTCP = atoi( it->second.c_str() );
        }
        else if( it->first.compare( "tmmbr" ) == 0 )
        {
            sendBitrateFeedback = atoi( it->second.c_str() );
            if( sendBitrateFeedback ) Log( "Activated bitrate feedback on %s stream %p.\n", MediaFrame::TypeToString( media ), this );
        }
        else if( it->first.compare( "ssrc" ) == 0 )
        {
            //Set ssrc for sending
            sendSSRC = atoi( it->second.c_str() );
        }
        else if( it->first.compare( "cname" ) == 0 )
        {
            //Check if already got one
            if( cname )
                //Delete it
                free( cname );
            //Clone
            cname = strdup( it->second.c_str() );
        }
        else if( it->first.compare( "useFEC" ) == 0 )
        {
            //Set fec decoding
            useFEC = atoi( it->second.c_str() );
        }
        else if( it->first.compare( "useNACK" ) == 0 )
        {
            //Set fec decoding
            useNACK = atoi( it->second.c_str() );
            //Enable NACK until first RTT
            isNACKEnabled = useNACK;

        }
        else if( it->first.compare( "useOriSeqNum" ) == 0 )
        {
            //Set numerotation of seqNum
            useOriSeqNum = atoi( it->second.c_str() );
            useOriTS = atoi( it->second.c_str() );
        }
        else if( it->first.compare( "useExtFIR" ) == 0 )
        {
            //Set use of SIP INFO FIR
            useExtFIR = atoi( it->second.c_str() );

        }
        else if( it->first.compare( "useRtcpFIR" ) == 0 )
        {
            //Set use of RTCP FIR
            useRtcpFIR = atoi( it->second.c_str() );

        }
        else if( it->first.compare( 0, 5, "codec" ) == 0 )
        {
            // Ignore codec props
            //Set extension
            extMap[RTPPacket::HeaderExtension::SSRCAudioLevel] = atoi( it->second.c_str() );
        }
        else if( it->first.compare( "urn:ietf:params:rtp-hdrext:toffset" ) == 0 )
        {
            //Set extension
            extMap[RTPPacket::HeaderExtension::TimeOffset] = atoi( it->second.c_str() );
        }
        else if( it->first.compare( "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time" ) == 0 )
        {
            //Set extension
            extMap[RTPPacket::HeaderExtension::AbsoluteSendTime] = atoi( it->second.c_str() );
            //Use timestamsp
            useAbsTime = true;
        }
        else
        {
            Error( "Unknown RTP property [%s]\n", it->first.c_str() );
        }
    }
    mutex.unlock();
    return 1;
}

int RTPSession::SetLocalSTUNCredentials( const char *username, const char *pwd )
{
    Log( "-SetLocalSTUNCredentials [frag:%s,pwd:%s]\n", username, "****" /*pwd*/ );
    //Clean mem
    if( iceLocalUsername )
        free( iceLocalUsername );
    if( iceLocalPwd )
        free( iceLocalPwd );
    //Store values
    iceLocalUsername = strdup( username );
    iceLocalPwd = strdup( pwd );
    //Ok
    return 1;
}

int RTPSession::SetRemoteSTUNCredentials( const char *username, const char *pwd )
{
    Log( "-SetRemoteSTUNCredentials [frag:%s,pwd:%s]\n", username, "****" /*pwd*/ );
    //Clean mem
    if( iceRemoteUsername )
        free( iceRemoteUsername );
    if( iceRemotePwd )
        free( iceRemotePwd );
    //Store values
    iceRemoteUsername = strdup( username );
    iceRemotePwd = strdup( pwd );
    //Ok
    return 1;
}

int RTPSession::SetRemoteCryptoDTLS( const char *setup, const char *hash, const char *fingerprint )
{
    Log( "-SetRemoteCryptoDTLS [setup:%s,hash:%s,fingerpritn:%s]\n", setup, hash, fingerprint );

    //Set Suite
    if( strcasecmp( setup, "active" ) == 0 )
        dtls.SetRemoteSetup( DTLSConnection::SETUP_ACTIVE );
    else if( strcasecmp( setup, "passive" ) == 0 )
        dtls.SetRemoteSetup( DTLSConnection::SETUP_PASSIVE );
    else if( strcasecmp( setup, "actpass" ) == 0 )
        dtls.SetRemoteSetup( DTLSConnection::SETUP_ACTPASS );
    else if( strcasecmp( setup, "holdconn" ) == 0 )
        dtls.SetRemoteSetup( DTLSConnection::SETUP_HOLDCONN );
    else
        return Error( "Unsupported setup mode [%s]\n", setup );

    //Set fingerprint
    if( strcasecmp( hash, "SHA-1" ) == 0 )
        dtls.SetRemoteFingerprint( DTLSConnection::SHA1, fingerprint );
    else if( strcasecmp( hash, "SHA-256" ) == 0 )
        dtls.SetRemoteFingerprint( DTLSConnection::SHA256, fingerprint );
    else
        return Error( "Unsuppoted hash type [%s]. Must me SHA-1 or SHA-256.\n", hash );

    //encript & decript
    encript = true;
    decript = true;

    //Init DTLS
    return dtls.Init();
}

int RTPSession::SetRemoteCryptoSDES( const char *suite, const BYTE *key, const DWORD len, int keyRank )
{
    err_status_t err;
    srtp_policy_t policy;

    Log( "-Set remote RTP SDES [suite:%s keyRank=%i]\n", suite, keyRank );

    //empty policy
    memset( &policy, 0, sizeof( srtp_policy_t ) );

    if( strcmp( suite, "AES_CM_128_HMAC_SHA1_80" ) == 0 )
    {
        crypto_policy_set_aes_cm_128_hmac_sha1_80( &policy.rtp );
        crypto_policy_set_aes_cm_128_hmac_sha1_80( &policy.rtcp );
    }
    else if( strcmp( suite, "AES_CM_128_HMAC_SHA1_32" ) == 0 )
    {
        crypto_policy_set_aes_cm_128_hmac_sha1_32( &policy.rtp );
        crypto_policy_set_aes_cm_128_hmac_sha1_80( &policy.rtcp );
    }
    else if( strcmp( suite, "AES_CM_128_NULL_AUTH" ) == 0 )
    {
        crypto_policy_set_aes_cm_128_null_auth( &policy.rtp );
        crypto_policy_set_aes_cm_128_null_auth( &policy.rtcp );
    }
    else if( strcmp( suite, "NULL_CIPHER_HMAC_SHA1_80" ) == 0 )
    {
        crypto_policy_set_null_cipher_hmac_sha1_80( &policy.rtp );
        crypto_policy_set_null_cipher_hmac_sha1_80( &policy.rtcp );
    }
    else
    {
        return Error( "Unknown cipher suite\n" );
    }

    //Check sizes
    if( len != policy.rtp.cipher_key_len )
        //Error
        return Error( "Key size (%d) doesn't match the selected srtp profile (required %d)\n", len, policy.rtp.cipher_key_len );


    //Set policy values
    policy.ssrc.type = ssrc_any_inbound;
    policy.ssrc.value = 0;
    policy.key = (BYTE *)key;
    policy.next = NULL;

    if( keyRank == 0 )
        //Create new
        err = srtp_create( &recvSRTPSession, &policy );
    else
        err = srtp_create( &recvSRTPSession_secondary, &policy );

    //Check error
    if( err != err_status_ok )
        //Error
        return Error( "Failed set remote SDES  (%d)\n", err );

    if( keyRank == 0 )
        //Create new
        err = srtp_create( &recvSRTPSessionRTX, &policy );
    else
        err = srtp_create( &recvSRTPSessionRTX_secondary, &policy );

    //Check error
    if( err != err_status_ok )
        //Error
        Error( "------------------------------------Failed set remote RTX SDES  (%d)\n", err );

    //Everything ok
    return 1;
}

int RTPSession::SetRemoteCryptoSDES( const char *suite, const char *key64, int keyRank )
{
    const char *sep;
    char key64copy[200];
    //Log
    Log( "-SetRemoteCryptoSDES [%p] [key:%s,suite:%s]\n", this, key64, suite );

    //Decript
    decript = true;

    //Get length
    WORD len64 = strlen( key64 );

    //Allocate memory for the key
    BYTE recvKey[len64];
    //Decode
    if( len64 > sizeof( key64copy ) )
    {
        return Error( "remote SDES key too long. Len = %d.\n", len64 );
    }

    sep = strchr( key64, '|' );
    if( sep != NULL )
    {
        len64 = sep - key64;
        Log( "-SetRemoteCryptoSDES found additional info. key=%.*s.\n", len64, key64 );
        strncpy( key64copy, key64, len64 );
        key64copy[len64] = 0;
        Log( "-SetRemoteCryptoSDES found additional info. key=%s.\n", key64copy );
    }
    else
    {
        strcpy( key64copy, key64 );
    }

    WORD len = av_base64_decode( recvKey, key64copy, len64 );
    if( len == (WORD)-1 )
    {
        Error( "-SetRemoteCryptoSDES: fail to decode base64 DES key %s len=%d.\n", key64, len64 );
        return 0;
    }

    //Set it
    return SetRemoteCryptoSDES( suite, recvKey, len, keyRank );
}

void RTPSession::SetReceivingRTPMap( RTPMap &map )
{
    //If we already have one
    if( rtpMapIn )
        //Delete it
        delete(rtpMapIn);
    //Clone it
    rtpMapIn = new RTPMap( map );
}

int RTPSession::SetLocalPort( int recvPort )
{
    //Override
    simPort = recvPort;
}

int RTPSession::GetLocalPort()
{
    // Return local
    return simPort;
}

int RTPSession::GetRemotePort()
{
    // Return local
    return sendAddr.sin_port;
}

bool RTPSession::SetSendingCodec( DWORD codec )
{
    //Check rtp map
    if( !rtpMapOut )
        //Error
        return Error( "-SetSendingCodec error: no out RTP map\n" );

    //Try to find it in the map
    for( RTPMap::iterator it = rtpMapOut->begin(); it != rtpMapOut->end(); ++it )
    {
        //Is it ourr codec
        if( it->second == codec )
        {
            //Get type
            DWORD type = it->first;
            //Log it
            Log( "-SetSendingCodec [codec:%s,type:%d]\n", GetNameForCodec( media, codec ), type );
            //Set type in header
            ((rtp_hdr_t *)sendPacket)->pt = type;
            //Set type
            sendType = type;
            //and we are done
            return true;
        }
    }

    //Not found
    return Error( "-SetSendingCodec error: codec mapping not found [codec:%s]\n", GetNameForCodec( media, codec ) );
}

/***********************************
* SetRemotePort
*	Inicia la sesion rtp de video remota
***********************************/
int RTPSession::SetRemotePort( char *ip, int sendPort )
{
    //Get ip addr
    DWORD ipAddr = inet_addr( ip );

    //If we already have one and it is a NATed
    if( recIP != INADDR_ANY && ipAddr == INADDR_ANY )
    {
        //Exit
        Log( "-SetRemotePort NAT already bound to [%s:%d] for media %s\n"
            , inet_ntoa( sendAddr.sin_addr )
            , recPort
            , MediaFrame::TypeToString( media )
            );
        return 1;
    }

    //Ok, let's et it
    Log( "-SetRemotePort [%s:%d] for media %s\n", ip, sendPort, MediaFrame::TypeToString( media ) );

    //Ip y puerto de destino
    sendAddr.sin_addr.s_addr = ipAddr;
    sendAddr.sin_port = htons( sendPort );

    sendRtcpAddr.sin_addr.s_addr = ipAddr;
    //Check if doing rtcp muxing
    if( muxRTCP )
        //Same than rtp
        sendRtcpAddr.sin_port = htons( sendPort );
    else
        //One more than rtp
        sendRtcpAddr.sin_port = htons( sendPort + 1 );

    //Open rtp and rtcp ports
    sendto( simSocket, rtpEmpty, sizeof( rtpEmpty ), 0, (sockaddr *)&sendAddr, sizeof( struct sockaddr_in ) );

    //Y abrimos los sockets	
    return 1;
}

int RTPSession::SendEmptyPacket()
{
    //Check if we have sendinf ip address
    if( sendAddr.sin_addr.s_addr == INADDR_ANY )
        //Exit
        return 0;

    //Open rtp and rtcp ports
    sendto( simSocket, rtpEmpty, sizeof( rtpEmpty ), 0, (sockaddr *)&sendAddr, sizeof( struct sockaddr_in ) );

    //ok
    return 1;
}

void RTPSession::SetRemoteRateEstimator( RemoteRateEstimator *estimator )
{
    Log( "-SetRemoteRateEstimator\n" );

    //Store it
    remoteRateEstimator = estimator;

    //Add as listener
    remoteRateEstimator->SetListener( this );
}

/********************************
* Init
*	Inicia el control rtcp
********************************/
int RTPSession::Init()
{
    int retries = 0;
    simPort = 0;
    Log( ">Init RTPSession %p for media %s and role %s\n", this, MediaFrame::TypeToString( media ), MediaFrame::RoleToString( role ) );

    sockaddr_in recAddr;

    //Clear addr
    memset( &recAddr, 0, sizeof( struct sockaddr_in ) );

    //Set family
    recAddr.sin_family = AF_INET;

    //Get two consecutive ramdom ports
    while( retries++ < 100 )
    {
        //If we have a rtp socket
        if( simSocket != FD_INVALID )
        {
            // Close first socket
            close( simSocket );
            //No socket
            simSocket = FD_INVALID;
        }
        //If we have a rtcp socket
        if( simRtcpSocket != FD_INVALID )
        {
            ///Close it
            close( simRtcpSocket );
            //No socket
            simRtcpSocket = FD_INVALID;
        }

        //Create new sockets
        simSocket = socket( PF_INET, SOCK_DGRAM, 0 );
        //If not forced to any port
        if( !simPort )
        {
            //Get random
            simPort = (int)(RTPSession::GetMinPort() + (RTPSession::GetMaxPort() - RTPSession::GetMinPort()) * double( rand() / double( RAND_MAX ) ));
            //Make even
            simPort &= 0xFFFFFFFE;
        }
        //Try to bind to port
        recAddr.sin_port = htons( simPort );
        //Bind the rtcp socket
        if( bind( simSocket, (struct sockaddr *)&recAddr, sizeof( struct sockaddr_in ) ) != 0 )
        {
            Log( "Failed to open RTP port %d : %s.\n", simPort, strerror( errno ) );
            simPort = 0;
            //Try again
            continue;
        }
        //Create new sockets
        simRtcpSocket = socket( PF_INET, SOCK_DGRAM, 0 );
        //Next port
        simRtcpPort = simPort + 1;
        //Try to bind to port
        recAddr.sin_port = htons( simRtcpPort );
        //Bind the rtcp socket
        if( bind( simRtcpSocket, (struct sockaddr *)&recAddr, sizeof( struct sockaddr_in ) ) != 0 )
        {
            //Use random
            simPort = 0;
            //Try again
            Log( "Failed to open RTCP port %d : %s.\n", simRtcpPort, strerror( errno ) );
            continue;
        }
        //Set COS
        int cos = 5;
        setsockopt( simSocket, SOL_SOCKET, SO_PRIORITY, &cos, sizeof( cos ) );
        setsockopt( simRtcpSocket, SOL_SOCKET, SO_PRIORITY, &cos, sizeof( cos ) );
        //Set TOS
        int tos = 0x2E;
        setsockopt( simSocket, IPPROTO_IP, IP_TOS, &tos, sizeof( tos ) );
        setsockopt( simRtcpSocket, IPPROTO_IP, IP_TOS, &tos, sizeof( tos ) );
        //Everything ok
        Log( "-Got ports [%d,%d]\n", simPort, simRtcpPort );
        //Start receiving
        Start();
        //Done
        Log( "<Init RTPSession\n" );
        //Opened
        return 1;
    }
    //Error
    Error( "RTPSession too many failed attemps opening sockets" );

    //Failed
    return 0;
}

/*********************************
* End
*	Termina la todo
*********************************/
int RTPSession::End()
{
    //Check if not running
    if( !running )
        //Nothing
        return 0;

    //Stop just in case
    Stop();

    //Not running;
    running = false;
    //If got socket
    if( simSocket != FD_INVALID )
    {
        //Will cause poll to return
        close( simSocket );
        //No sockets
        simSocket = FD_INVALID;
    }
    if( simRtcpSocket != FD_INVALID )
    {
        //Will cause poll to return
        close( simRtcpSocket );
        //No sockets
        simRtcpSocket = FD_INVALID;
    }

    return 1;
}

int RTPSession::SendPacket( RTCPCompoundPacket &rtcp )
{
    BYTE data[MTU + SRTP_MAX_TRAILER_LEN] ZEROALIGNEDTO32;
    DWORD size = RTPPAYLOADSIZE;
    int ret = 0;

    //Check if we have sendinf ip address
    if( sendRtcpAddr.sin_addr.s_addr == INADDR_ANY && !muxRTCP )
    {
        //Debug
        Debug( "-Error sending rtcp packet, no remote IP yet\n" );
        //Exit
        return 0;
    }
    //Serialize
    int len = rtcp.Serialize( data, size );
    //Check result
    if( !len )
        //Error
        return Error( "Error serializing RTCP packet\n" );

    //If encripted
    if( encript )
    {
        //Check  session
        if( !sendSRTPSession )
            return Error( "-no sendSRTPSession\n" );
        //Protect
        err_status_t err = srtp_protect_rtcp( sendSRTPSession, data, &len );
        //Check error
        if( err != err_status_ok )
            //Nothing
            return Error( "Error protecting RTCP packet [%d]\n", err );
    }

    //If muxin
    if( muxRTCP )
        //Send using RTP port
        ret = sendto( simSocket, data, len, 0, (sockaddr *)&sendAddr, sizeof( struct sockaddr_in ) );
    else
        //Send using RCTP port
        ret = sendto( simRtcpSocket, data, len, 0, (sockaddr *)&sendRtcpAddr, sizeof( struct sockaddr_in ) );

    //Check error
    if( ret < 0 )
    {
        int err = errno;

        Error( "-Error sending RTP packet [%d]\n", err );
        //Return
        return 0;
    }

    //Exit
    return 1;
}

int RTPSession::SendPacket( RTPPacket &packet )
{
    return SendPacket( packet, packet.GetTimestamp() );
}

int RTPSession::SendPacket( RTPPacket &packet, DWORD timestamp )
{
    //If session is not running drop the packet silently
    if( !running ) return 0;

    //Check if we have sending ip address
    if( sendAddr.sin_addr.s_addr == INADDR_ANY )
    {
        //Do we have rec ip?
        /*            
        if( recIP == INADDR_ANY )
        {
            recIP = inet_addr( "172.21.100.19" );
            recPort = ntohs( sendAddr.sin_port );
        }
        */

        if( recIP != INADDR_ANY )
        {
            //Do NAT
            sendAddr.sin_addr.s_addr = recIP;
            //Set port
            sendAddr.sin_port = htons( recPort );
            //Log
            Log( "-RTPSession NAT: now sending %s to [%s:%d].\n"
                , MediaFrame::TypeToString( media )
                , inet_ntoa( sendAddr.sin_addr )
                , recPort
                );
            //Check if using ice
        }
        else
        {
            Debug( "-No remote address for [%s]\n", MediaFrame::TypeToString( media ) );
            //Exit
            return 0;
        }
    }
    else if( recIP != INADDR_ANY && recIP != sendAddr.sin_addr.s_addr )
    {
        // sendAddr.sin_addr.s_addr = recIP;
        // sendAddr.sin_port = htons(recPort);
        Log( "-RTPSession NAT: WARNING Trying to send packet from different ip address than receiving one.\n" );
    }

    //Check if we need to send SR
    if( isZeroTime( &lastSR ) || (getDifTime( &lastSR ) > 4000000 /*us*/) )
        //Send it
        SendSenderReport();

    //Modificamos las cabeceras del packete
    rtp_hdr_t *headers = (rtp_hdr_t *)sendPacket;

    // if the codec of the packet we want to send is not the same than defined, we changed it
    if( rtpMapOut->find( sendType ) == rtpMapOut->end() || (*rtpMapOut)[sendType] != packet.GetCodec() )
    {
        this->SetSendingCodec( packet.GetCodec() );
    }

    //Init send packet
    headers->version = RTP_VERSION;

    //if we detect a change of ssrc in packet, we change the ssrc
    if( lastSendSSRC == 0 )
    {
        sendSSRC = RandomUInt32( (unsigned int)time( NULL ) + (unsigned int)(uint64_t)this );
        Debug( "Intializing sending SSRC - send=0x%X, seq=%d\n", sendSSRC, packet.GetSeqNum() );
    } 
    else if( lastSendSSRC != packet.GetSSRC() )
    {
        sendSSRC = RandomUInt32();
        Debug( "Changing sending SSRC - last=0x%X, packet=0x%X, seq=%d\n", lastSendSSRC, packet.GetSSRC(), packet.GetSeqNum() );
    }

    headers->ssrc = htonl( sendSSRC );
    lastSendSSRC = packet.GetSSRC();

/*
    //Simulate SSRC change - for test purporse only
    if ( (sendSeq%50) == 0
       || (sendSeq%50) == 1
       || (sendSeq%50) == 4 )
    {
        headers->ssrc = RandomUInt32();
    }
*/
    // in case of bridging, we don't change the timestamp of the packet.
    if( useOriTS && this->media != MediaFrame::Text )
    {
        sendLastTime = packet.GetTimestamp();
    }
    else
    {
        //Calculate last timestamp
        sendLastTime = sendTime + timestamp;

    }
    headers->ts = htonl( sendLastTime );

    //Incrementamos el numero de secuencia
    // in case of bridging , we don't change the seq num of the packet.
    if( useOriSeqNum && this->media != MediaFrame::Text )
    {
        sendSeq = packet.GetSeqNum();
        headers->seq = htons( sendSeq );
    }
    else
    {
        headers->seq = htons( sendSeq );
        sendSeq++;
    }

    //Check seq wrap
    if( sendSeq == 0 )
    {
        //Inc cycles
        sendCycles++;
    }

    //La marca de fin de frame
    headers->m = packet.GetMark();

    //Calculamos el inicio
    int ini = sizeof( rtp_hdr_t );

    //If we have are using any sending extensions
    if( useAbsTime )
    {
        //Get header
        rtp_hdr_ext_t *ext = (rtp_hdr_ext_t *)(sendPacket + ini);
        //Set extension header
        headers->x = 1;
        //Set magic cookie
        ext->ext_type = htons( 0xBEDE );
        //Set total length in 32bits words
        ext->len = htons( 1 );
        //Increase ini
        ini += sizeof( rtp_hdr_ext_t );
        //Calculate absolute send time field (convert ms to 24-bit unsigned with 18 bit fractional part.
        // Encoding: Timestamp is in seconds, 24 bit 6.18 fixed point, yielding 64s wraparound and 3.8us resolution (one increment for each 477 bytes going out on a 1Gbps interface).
        DWORD abs = ((getTimeMS() << 18) / 1000) & 0x00ffffff;
        //Set header
        sendPacket[ini] = extMap.GetCodecForType( RTPPacket::HeaderExtension::AbsoluteSendTime ) << 4 | 0x02;
        //Set data
        set3( sendPacket, ini + 1, abs );
        //Increase ini
        ini += 4;
    }

    //Comprobamos que quepan
    if( ini + packet.GetMediaLength() > MTU )
        return Error( "SendPacket Overflow [size:%d, max:%d]\n", ini + packet.GetMediaLength(), MTU );

    //Copiamos los datos
    memcpy( sendPacket + ini, packet.GetMediaData(), packet.GetMediaLength() );

    //Set pateckt length
    int len = packet.GetMediaLength() + ini;

    //Check if we are encripted
    if( encript )
    {
        if( !sendSRTPSession )
        {
            Debug( "-RTPSession: encryption is not yet setup.\n" );
            return 0;
        }
        err_status_t err;

        //Encript
        err = srtp_protect( sendSRTPSession, sendPacket, &len );
        //Check error
        if( err != err_status_ok )
        {
            //Nothing
            Error( "Error protecting RTP packet for %s with recSSRC=%x and for session=%p : [%d]\n", MediaFrame::TypeToString( media ), packet.GetSSRC(), this, err );

            return -1;
        }
    }

    //Add it rtx queue
    if( useNACK )
    {
        //Create new pacekt
        RTPTimedPacket *rtx = new RTPTimedPacket( media, sendPacket, len );

        rtxUse.WaitUnusedAndLock();

        //Set cycles
        rtx->SetSeqCycles( sendCycles );
        //Add it to que
        rtxs[rtx->GetExtSeqNum()] = rtx;

        RTPOrderedPackets::iterator it = rtxs.begin();
        if( it != rtxs.end() )
        {
            RTPTimedPacket *pkt = it->second;
            if( rtxs.size() > 200 )
            {
                rtxs.erase( it );
                delete pkt;
            }
        }
        //Unlock
        rtxUse.Unlock();
    }

    //SIMULATING PACKET LOST 10%	
    /*
    int ret =0;
    if (this->media == MediaFrame::Video  && (sendSeq%10) == 0)
    {
        ret =0;
    }
    else
    {
        //Send packet
        ret = sendto(simSocket,sendPacket,len,0,(sockaddr *)&sendAddr,sizeof(struct sockaddr_in));
    }
    */

    //Send packet
    int ret = sendto( simSocket, sendPacket, len, 0, (sockaddr *)&sendAddr, sizeof( struct sockaddr_in ) );
    if( ret <= 0 )
    {
        Log( "Failed to send RTP packet seqnum %d, errno=%d.\n", sendSeq, errno );
    }
    else
    {
        //Inc stats
        numSendPackets++;
        totalSendBytes += packet.GetMediaLength();
    }

    //Exit
    return (ret > 0);
}

int RTPSession::ReadRTCP()
{
    BYTE buffer[MTU];
    sockaddr_in from_addr;
    DWORD from_len = sizeof( from_addr );

    //Receive from everywhere
    memset( &from_addr, 0, from_len );

    //Read rtcp socket
    int size = recvfrom( simRtcpSocket, buffer, MTU, MSG_DONTWAIT, (sockaddr *)&from_addr, &from_len );

    //Check if it is an STUN request
    STUNMessage *stun = STUNMessage::Parse( buffer, size );
    if( stun )
    {
        STUNMessage::Type type = stun->GetType();
        STUNMessage::Method method = stun->GetMethod();

        //If it is a request
        if( type == STUNMessage::Request && method == STUNMessage::Binding && iceLocalPwd )
        {
            DWORD len = 0;
            //Create response
            STUNMessage *resp = stun->CreateResponse();
            //Add received xor mapped addres
            resp->AddXorAddressAttribute( &from_addr );
            //TODO: Check incoming request username attribute value starts with iceLocalUsername+":"
            //Create  response
            DWORD size = resp->GetSize();
            BYTE *aux = (BYTE *)malloc( size );

            //Check if we have local passworkd
            if( iceLocalPwd )
                //Serialize and autenticate
                len = resp->AuthenticatedFingerPrint( aux, size, iceLocalPwd );
            else
                //Do nto authenticate
                len = resp->NonAuthenticatedFingerPrint( aux, size );

            //Send it
            sendto( simRtcpSocket, aux, len, 0, (sockaddr *)&from_addr, sizeof( struct sockaddr_in ) );

            //Clean memory
            free( aux );
            //Clean response
            delete(resp);

            //Do NAT
            sendRtcpAddr.sin_addr.s_addr = from_addr.sin_addr.s_addr;
            //Set port
            sendRtcpAddr.sin_port = from_addr.sin_addr.s_addr;
        }

        //Delete message
        delete(stun);
        //Exit
        return 1;
    }

    //Check if it is RTCP
    if( !RTCPCompoundPacket::IsRTCP( buffer, size ) )
        //Exit
        return 0;

    //Check if we have sendinf ip address
    if( sendRtcpAddr.sin_addr.s_addr == INADDR_ANY )
    {
        //Do NAT
        sendRtcpAddr.sin_addr.s_addr = from_addr.sin_addr.s_addr;
        //Set port
        sendRtcpAddr.sin_port = from_addr.sin_port;
        //Log it
        Log( "-Got first RTCP packet, sending to %s:%d with rtcp-muxing %s\n",
            inet_ntoa( sendRtcpAddr.sin_addr ), ntohs( sendRtcpAddr.sin_port ),
            muxRTCP ? "on" : "off" );
    }

    //Decript
    if( decript )
    {
        if( !recvSRTPSession )
            return Error( "-No recvSRTPSession yet (RTCP)\n" );
        //unprotect
        err_status_t err = srtp_unprotect_rtcp( recvSRTPSession, buffer, &size );
        //Check error
        if( err != err_status_ok )
            return Error( "Error unprotecting rtcp packet [%d]\n", err );
    }
    //RTCP mux disabled
    muxRTCP = false;
    //Parse it
    RTCPCompoundPacket *rtcp = RTCPCompoundPacket::Parse( buffer, size );
    //Check packet
    if( rtcp )
        //Handle incomming rtcp packets
        ProcessRTCPPacket( rtcp, inet_ntoa( from_addr.sin_addr ) );

    //OK
    return 1;
}

/*********************************
* GetTextPacket
*	Lee el siguiente paquete de video
*********************************/
int RTPSession::ReadRTP()
{
    BYTE data[MTU];
    BYTE *buffer = data;
    sockaddr_in from_addr;
    bool isRTX = false;
    DWORD from_len = sizeof( from_addr );

    //Receive from everywhere
    memset( &from_addr, 0, from_len );

    //Leemos del socket
    int size = recvfrom( simSocket, buffer, MTU, MSG_DONTWAIT, (sockaddr *)&from_addr, &from_len );
    if( size <= 0 )
    {
        Error( "ReadRTP on fd %d error: errno=%d.\n", simSocket, errno );
        if( errno == ENOTCONN )
        {
            Error( "-fd is not valid. Stoppong RTP session %p.\n", this );
            running = false;
        }
        return 0;
    }

    //Check if it is an STUN request
    STUNMessage *stun = STUNMessage::Parse( buffer, size );
    if( stun )
    {
        STUNMessage::Type type = stun->GetType();
        STUNMessage::Method method = stun->GetMethod();

        //If it is a request
        if( type == STUNMessage::Request && method == STUNMessage::Binding )
        {
            DWORD len = 0;
            //Create response
            STUNMessage *resp = stun->CreateResponse();
            //Add received xor mapped addres
            resp->AddXorAddressAttribute( &from_addr );
            //TODO: Check incoming request username attribute value starts with iceLocalUsername+":"
            //Create  response
            DWORD size = resp->GetSize();
            BYTE *aux = (BYTE *)malloc( size );

            //Check if we have local passworkd
            Debug( "ICE: receiving Binding Request from %s localPwd=%s\n", inet_ntoa( from_addr.sin_addr ),
                (iceLocalPwd != NULL) ? "****" /*iceLocalPwd*/ : "no password" );
            if( iceRemotePwd )
            {
                if( iceLocalPwd )
                    //Serialize and autenticate
                    len = resp->AuthenticatedFingerPrint( aux, size, iceLocalPwd );
                else
                    //Do nto authenticate
                    len = resp->NonAuthenticatedFingerPrint( aux, size );
                if( !len )
                {
                    Debug( "ICE: packet empty no need to send it\n" );
                    return 0;
                }

                //Send it
                sendto( simSocket, aux, len, 0, (sockaddr *)&from_addr, sizeof( struct sockaddr_in ) );
            }
            else
            {
                Debug( "ICE: No iceRemotePwd defined yet. Dropping request...\n" );
            }

            //Clean memory
            free( aux );
            //Clean response
            delete(resp);

            if( iceRemoteIP == INADDR_ANY )
            {
                iceRemoteIP = from_addr.sin_addr.s_addr;
            }

            //If set
            if( stun->HasAttribute( STUNMessage::Attribute::IceControlled ) || 
                stun->HasAttribute( STUNMessage::Attribute::UseCandidate ) || 
                iceRemoteIP == from_addr.sin_addr.s_addr )
            {
                // We should check that username matches
                if( iceRemoteUsername )
                {
                    // ICE is enabled
                    if( recIP == INADDR_ANY )
                    {
                        // set recIP if not set
                        recIP = from_addr.sin_addr.s_addr;
                        recPort = ntohs( from_addr.sin_port );

                        //Log
                        Log( "-RTPSession STUN: received packet from [%s:%d] for media %s\n"
                            , inet_ntoa( from_addr.sin_addr )
                            , ntohs( from_addr.sin_port )
                            , MediaFrame::TypeToString( media )
                            );
                    }

                    if( sendAddr.sin_addr.s_addr != recIP || sendAddr.sin_port != htons( recPort ) )
                    {
                        // Do symetric RTP 
                        sendAddr.sin_addr.s_addr = recIP;
                        sendAddr.sin_port = ntohs( recPort );
                    }
                }

                DWORD len = 0;
                //Create trans id
                BYTE transId[12];
                //Set first to 0
                set4( transId, 0, 0 );
                //Set timestamp as trans id
                set8( transId, 4, getTime() );
                //Create binding request to send back
                STUNMessage *request = new STUNMessage( STUNMessage::Request, STUNMessage::Binding, transId );
                //Check usernames
                if( iceLocalUsername && iceRemoteUsername )
                    //Add username
                    request->AddUsernameAttribute( iceLocalUsername, iceRemoteUsername );
                    //Add other attributes
                if( stun->HasAttribute( STUNMessage::Attribute::IceControlled ) )
                {
                    request->AddAttribute( STUNMessage::Attribute::IceControlling, (QWORD)-1 );
                    request->AddAttribute( STUNMessage::Attribute::UseCandidate );
                }
                else
                    request->AddAttribute( STUNMessage::Attribute::IceControlled, (QWORD)-1 );

                request->AddAttribute( STUNMessage::Attribute::Priority, (DWORD)33554431 );
                //Create  request
                DWORD size = request->GetSize();
                BYTE *aux = (BYTE *)malloc( size );

                //Check remote pwd
                if( iceRemotePwd )
                {
                    Debug( "ICE: sending bind request with remote user=[%s], remote password=[%s] to %s:%d.\n",
                        (iceRemoteUsername != NULL) ? iceRemoteUsername : "no user",
                        (iceRemotePwd != NULL) ? "****" /*iceRemotePwd*/ : "no pwd",
                        inet_ntoa( from_addr.sin_addr ), ntohs( from_addr.sin_port ) );
                    if( iceRemotePwd )
                    {
                        //Serialize and autenticate
                        len = request->AuthenticatedFingerPrint( aux, size, iceRemotePwd );
                    }
                    else
                    {
                        //Do nto authenticate
                        len = request->NonAuthenticatedFingerPrint( aux, size );
                    }

                    //Send it
                    if( !len )
                    {
                        Debug( "ICE: packet empty no need to send it\n" );
                        return 0;
                    }

                    sendto( simSocket, aux, len, 0, (sockaddr *)&from_addr, sizeof( struct sockaddr_in ) );
                }

                //Clean memory
                free( aux );
                //Clean response
                delete(request);

                // Needed for DTLS in client mode (otherwise the DTLS "Client Hello" is not sent over the wire)
                len = dtls.Read( buffer, MTU );
                //Send back
                if( !len )
                {
                    Debug( "DTLS: packet empty no need to send it\n" );
                    return 0;
                }
                sendto( simSocket, buffer, len, 0, (sockaddr *)&from_addr, sizeof( struct sockaddr_in ) );
            }
        }

        //Delete message
        delete(stun);
        //Exit
        return 1;
    }

    //Check if it is RTCP
    if( RTCPCompoundPacket::IsRTCP( buffer, size ) )
    {
        //Decript
        if( decript )
        {
            if( !recvSRTPSession )
                return Error( "-No recvSRTPSession yet (RTCP)\n" );
            //unprotect
            err_status_t err = srtp_unprotect_rtcp( recvSRTPSession, buffer, &size );
            //Check error
            if( err != err_status_ok )
                return Error( "Error unprotecting rtcp packet [%d]\n", err );
        }
        //RTCP mux enabled
        muxRTCP = true;
        //Parse it
        RTCPCompoundPacket *rtcp = RTCPCompoundPacket::Parse( buffer, size );
        //Check packet
        if( rtcp )
            //Handle incomming rtcp packets
            ProcessRTCPPacket( rtcp, inet_ntoa( from_addr.sin_addr ) );

        //Skip
        return 1;
    }

    //Check if it a DTLS packet
    if( DTLSConnection::IsDTLS( buffer, size ) )
    {
        Log( "-RTPSession DTLS: received packet from [%s:%d] for media %s\n"
            , inet_ntoa( from_addr.sin_addr )
            , ntohs( from_addr.sin_port )
            , MediaFrame::TypeToString( media )
            );
        //Feed it
        if( !dtls.Write( buffer, size ) )
        {
            Debug( "-RTPSession DTLS: nothing to send back.\n" );
            //Exit
            return 0;
        }
        //Read
        int len = dtls.Read( buffer, MTU );
        //Send it back
        if( !len )
        {
            Debug( "-RTPSession DTLS: packet empty no need to send it\n" );
            return 1;
        }

        sendto( simSocket, buffer, len, 0, (sockaddr *)&from_addr, sizeof( struct sockaddr_in ) );
        return 1;
    }

    //Double check it is an RTP packet
    if( !RTPPacket::IsRTP( buffer, size ) )
    {
        //Debug
        Debug( "-Not RTP data received\n" );
        //Exit
        return 1;
    }

    //If we don't have originating IP
    if( recIP != from_addr.sin_addr.s_addr )
    {
        //Bind it to first received packet ip
        recIP = from_addr.sin_addr.s_addr;
        //Get also port
        recPort = ntohs( from_addr.sin_port );
        //Log
        Log( "-RTPSession NAT: received packet from [%s:%d] for media %s\n"
            , inet_ntoa( from_addr.sin_addr )
            , ntohs( from_addr.sin_port )
            , MediaFrame::TypeToString( media )
            );
        //Check if got listener
        if( listener )
            //Request a I frame
            listener->onFPURequested( this );
    }

    //Check minimum size for rtp packet
    if( size < 12 )
        return Error( "Invalid RTP packet read from fd %d. Packet too small of %d bytes.\n", simSocket, size );

    DWORD defaultSSRC = GetDefaultStream( true );

    //This should be improbed
    /*if (useNACK && defaultSSRC && defaultSSRC!=RTPPacket::GetSSRC(buffer))
    {
        Debug("-----nacked %x %x\n",defaultSSRC,RTPPacket::GetSSRC(buffer));
        //It is a retransmited packet
        isRTX = true;
    }*/

    //Check if it is encripted
    if( decript )
    {
        err_status_t err;
        //Check if it is a retransmited packet

        //Check session
        if( !recvSRTPSession )
            return Error( "-No recvSRTPSession yet\n" );

        if( !isRTX )
        {
            //unprotect
            if( defaultSSRC == 0 || defaultSSRC == RTPPacket::GetSSRC( buffer ) )
                err = srtp_unprotect( recvSRTPSession, buffer, &size );
            else
            {
                if( recvSRTPSession_secondary )
                    err = srtp_unprotect( recvSRTPSession_secondary, buffer, &size );
            }
        }
        else
        {
            //unprotect RTX
            if( defaultSSRC == 0 || defaultSSRC == RTPPacket::GetSSRC( buffer ) )
                err = srtp_unprotect( recvSRTPSessionRTX, buffer, &size );
            else
            {
                if( recvSRTPSessionRTX_secondary )
                    err = srtp_unprotect( recvSRTPSessionRTX_secondary, buffer, &size );
            }
        }

        //Check status
        if( err != err_status_ok )
            //Error
            return Error( "Error unprotecting rtp packet [%d] for RTPSession=%p, ssrc=%x defaultSSRC=%x\n", err, this, RTPPacket::GetSSRC( buffer ), defaultSSRC );
    }

    //If it is a retransmission
    if( isRTX )
    {
        //Get original sequence number
        WORD osn = get2( buffer, sizeof( rtp_hdr_t ) );
        //Move origin
        for( int i = sizeof( rtp_hdr_t ) - 1; i >= 0; --i )
            //Move
            buffer[i + 2] = buffer[i];
        //Move init
        buffer += 2;
        //Set original seq num
        set2( buffer, 2, osn );
    }

    //Get type
    BYTE type = RTPPacket::GetType( buffer );

    //Check rtp map
    if( !rtpMapIn )
        //Error
        return Error( "-RTP map not set\n" );

    //Set initial codec
    BYTE codec = rtpMapIn->GetCodecForType( type );

    //Check codec
    if( codec == RTPMap::NotFound )
        //Exit
        return Error( "-RTP packet type unknown [%d]\n", type );

    //Create rtp packet
    RTPTimedPacket *packet = NULL;

    //Peek type
    if( codec == TextCodec::T140RED || codec == VideoCodec::RED )
    {
        //Create redundant type
        RTPRedundantPacket *red = new RTPRedundantPacket( media, buffer, size );
        //Get primary type
        BYTE t = red->GetPrimaryType();
        //Map primary data codec
        BYTE c = rtpMapIn->GetCodecForType( t );
        //Check codec
        if( c == RTPMap::NotFound )
        {
            //Delete red packet
            delete(red);
            //Exit
            return Error( "-RTP packet type unknown for primary type of redundant data [%d]\n", t );
        }
        //Set it
        red->SetPrimaryCodec( c );
        //For each redundant packet
        for( int i = 0; i < red->GetRedundantCount(); ++i )
        {
            //Get redundant type
            BYTE t = red->GetRedundantType( i );
            //Map redundant data codec
            BYTE c = rtpMapIn->GetCodecForType( t );
            //Check codec
            if( c == RTPMap::NotFound )
            {
                //Delete red packet
                delete(red);
                //Exit
                return Error( "-RTP packet type unknown for primary type of secundary data [%d,%d]\n", i, t );
            }
            //Set it
            red->SetRedundantCodec( i, c );
        }
        //Set packet
        packet = red;
    }
    else
    {
        //Create normal packet
        packet = new RTPTimedPacket( media, buffer, size );
    }
        //Set codec
    packet->SetCodec( codec );
    //Get ssrc
    DWORD ssrc = packet->GetSSRC();

    streamUse.IncUse();

    //if ( defaultSSRC == 0 && defaultStream != NULL) defaultStream->Cancel();

    RTPStream *stream = getStream( ssrc );

    if( !isRTX )
    {
        if( stream == NULL )
        {
            //Send SR to old one
            SendSenderReport();
            streamUse.DecUse();
            if( defaultStream == NULL && ssrc > 0 )
            {
                Log( "-Creating default stream SSRC [new:0x%X] for RTPSession=%p \n", ssrc, this );
                SetDefaultStream( true, ssrc );
            }
            else if( listener ) //call listener
            {
                listener->onNewStream( this, ssrc, true );
            }
            else
            {
                Log( "-No Listener. Adding new SSRC [new:0x%X]\n", ssrc );
                if( defaultStream == NULL )
                    SetDefaultStream( true, ssrc );
                else
                    ChangeStream( defaultSSRC, ssrc );
            }

            streamUse.IncUse();
            stream = getStream( ssrc );
        }

        if( stream != NULL )
        {
            stream->Add( packet, size );
        }
    }
    else /* RTP retransmission enabled. We do not handle multi stream in that case */
    {
        if( defaultSSRC > 0 )
        {
            //Set SSRC of the original stream for retransmitted packets
            if( ssrc != defaultSSRC ) packet->SetSSRC( defaultSSRC );
        }
        else
        {
            streamUse.DecUse();
            SetDefaultStream( true, ssrc );
            streamUse.IncUse();
        }
        defaultStream->Add( packet, size );
    }
    streamUse.DecUse();
    //OK
    return 1;
}

void RTPSession::Start()
{
    //We are running
    running = true;

    //Create thread
    createPriorityThread( &thread, run, this, 0 );
}

void RTPSession::Stop()
{
    //Check thred
    if( thread )
    {
        //Not running
        running = false;

        //Signal the pthread this will cause the poll call to exit
        pthread_kill( thread, SIGIO );
        //Wait thread to close
        pthread_join( thread, NULL );
        //Nulifi thread
        thread = 0;
        DeleteStreams();
    }

    rtxUse.WaitUnusedAndLock();
    for( RTPOrderedPackets::iterator it = rtxs.begin(); it != rtxs.end(); ++it )
    {
        //Get pacekt
        RTPTimedPacket *pkt = it->second;
        //Delete object
        delete(pkt);
    }
    rtxUse.Unlock();
}

/***********************
* run
*       Helper thread function
************************/
void *RTPSession::run( void *par )
{
    Log( "-RTPSession thread [%d,0x%x]\n", getpid(), par );

    //Block signals to avoid exiting on SIGUSR1
    blocksignals();

        //Obtenemos el parametro
    RTPSession *sess = (RTPSession *)par;

    //Ejecutamos
    pthread_exit( (void *)sess->Run() );
}

/***************************
 * Run
 * 	Server running thread
 ***************************/
int RTPSession::Run()
{
    Log( ">Run RTPSession [%p]\n", this );

    //Set values for polling
    ufds[0].fd = simSocket;
    ufds[0].events = POLLIN | POLLERR | POLLHUP;
    ufds[1].fd = simRtcpSocket;
    ufds[1].events = POLLIN | POLLERR | POLLHUP;

    //Set non blocking so we can get an error when we are closed by end
    int fsflags = fcntl( simSocket, F_GETFL, 0 );
    fsflags |= O_NONBLOCK;
    fcntl( simSocket, F_SETFL, fsflags );

    fsflags = fcntl( simRtcpSocket, F_GETFL, 0 );
    fsflags |= O_NONBLOCK;
    fcntl( simRtcpSocket, F_SETFL, fsflags );

    //Catch all IO errors
    signal( SIGIO, EmptyCatch );

    //Run until ended
    while( running )
    {
        //Wait for events
        if( poll( ufds, 2, -1 ) < 0 )
            //Check again
            continue;

        if( ufds[0].revents & POLLIN )
            //Read rtp data
            ReadRTP();
        if( ufds[1].revents & POLLIN )
            //Read rtcp data
            ReadRTCP();

        if( (ufds[0].revents & POLLHUP) || (ufds[0].revents & POLLERR) || (ufds[1].revents & POLLHUP) || (ufds[0].revents & POLLERR) )
        {
            //Error
            Log( "Pool error event [%d]\n", ufds[0].revents );
            //Exit
            break;
        }
    }

    Log( "<Run RTPSession\n" );
}

RTPPacket *RTPSession::GetPacket()
{
    streamUse.IncUse();
    RTPPacket *rtp = (defaultStream != NULL && !defaultStream->disabled) ? defaultStream->Wait() : NULL;
    streamUse.DecUse();
    if( rtp == NULL ) msleep( 100 );
    return rtp;
}

RTPPacket *RTPSession::GetPacket( DWORD &ssrc )
{
    streamUse.IncUse();
    RTPStream *s = (ssrc != 0) ? getStream( ssrc ) : defaultStream;
    streamUse.DecUse();
    RTPPacket *rtp = (s != NULL && !s->disabled) ? s->Wait() : NULL;
    if( rtp == NULL ) msleep( 100 );
    return rtp;
}

void RTPSession::CancelGetPacket()
{
    //cancel
    streamUse.IncUse();
    if( defaultStream != NULL ) defaultStream->Cancel();
    streamUse.DecUse();
}

void RTPSession::CancelGetPacket( DWORD &ssrc )
{
    streamUse.IncUse();
    RTPStream *s = (ssrc != 0) ? getStream( ssrc ) : defaultStream;
    if( s ) s->Cancel();
    streamUse.DecUse();
}

void RTPSession::ResetPacket( DWORD &ssrc, bool clear )
{
    streamUse.IncUse();
    RTPStream *s = (ssrc != 0) ? getStream( ssrc ) : defaultStream;
    if( s ) s->Reset( clear );
    streamUse.DecUse();
}

void RTPSession::ProcessRTCPPacket( RTCPCompoundPacket *rtcp, const char *fromAddr )
{
    //For each packet
    for( int i = 0; i < rtcp->GetPacketCount(); i++ )
    {
        //Get pacekt
        RTCPPacket *packet = rtcp->GetPacket( i );
        //Check packet type
        switch( packet->GetType() )
        {
            case RTCPPacket::SenderReport:
            {
                RTCPSenderReport *sr = (RTCPSenderReport *)packet;
                //Get Timestamp, the middle 32 bits out of 64 in the NTP timestamp (as explained in Section 4) received as part of the most recent RTCP sender report (SR) packet from source SSRC_n. If no SR has been received yet, the field is set to zero.
                recSR = sr->GetTimestamp() >> 16;
                //Uptade last received SR
                getUpdDifTime( &lastReceivedSR );
                //Check recievd report
                for( int j = 0; j < sr->GetCount(); j++ )
                {
                    //Get report
                    RTCPReport *report = sr->GetReport( j );
                    //Check ssrc
                    if( report->GetSSRC() == sendSSRC )
                    {
                        //Calculate RTT
                        if( !isZeroTime( &lastSR ) && report->GetLastSR() == sendSR )
                        {
                            //Calculate new rtt
                            DWORD rtt = getDifTime( &lastSR ) / 1000 - report->GetDelaySinceLastSRMilis();
                            //Set it
                            SetRTT( rtt );
                        }
                    }
                }
                break;
            }
            case RTCPPacket::ReceiverReport:
            {
                RTCPReceiverReport *rr = (RTCPReceiverReport *)packet;
                //Check recievd report
                for( int j = 0; j < rr->GetCount(); j++ )
                {
                    //Get report
                    RTCPReport *report = rr->GetReport( j );
                    //Check ssrc
                    if( report->GetSSRC() == sendSSRC )
                    {
                        //Calculate RTT
                        if( !isZeroTime( &lastSR ) && report->GetLastSR() == sendSR )
                        {
                            //Calculate new rtt
                            DWORD rtt = getDifTime( &lastSR ) / 1000 - report->GetDelaySinceLastSRMilis();
                            //Set it
                            SetRTT( rtt );
                        }
                    }
                }
                break;
            }
            case RTCPPacket::SDES:
                break;
            case RTCPPacket::Bye:
                break;
            case RTCPPacket::App:
                break;
            case RTCPPacket::RTPFeedback:
            {
                //Get feedback packet
                RTCPRTPFeedback *fb = (RTCPRTPFeedback *)packet;
                //Check feedback type
                switch( fb->GetFeedbackType() )
                {
                    case RTCPRTPFeedback::NACK:
                        //Debug("-Nack received\n");
                        for( BYTE i = 0; i < fb->GetFieldCount(); i++ )
                        {
                            //Get field
                            RTCPRTPFeedback::NACKField *field = (RTCPRTPFeedback::NACKField *)fb->GetField( i );
                            //Resent it
                            ReSendPacket( field->pid );
                            //Check each bit of the mask
                            for( int i = 0; i < 16; i++ )
                                //Check it bit is present to rtx the packets
                                if( (field->blp >> (15 - i)) & 1 )
                                    //Resent it
                                    ReSendPacket( field->pid + i + 1 );
                        }
                        break;
                    case RTCPRTPFeedback::TempMaxMediaStreamBitrateRequest:
                        Log( "-TempMaxMediaStreamBitrateRequest received from [%s] on %s stream\n", fromAddr, MediaFrame::TypeToString( media ) );
                        for( BYTE i = 0; i < fb->GetFieldCount(); i++ )
                        {
                            //Get field
                            RTCPRTPFeedback::TempMaxMediaStreamBitrateField *field = (RTCPRTPFeedback::TempMaxMediaStreamBitrateField *)fb->GetField( i );
                            //Check if it is for us
                            if( listener && field->GetSSRC() == sendSSRC )
                                //call listener
                                listener->onTempMaxMediaStreamBitrateRequest( this, field->GetBitrate(), field->GetOverhead() );
                        }
                        break;
                    case RTCPRTPFeedback::TempMaxMediaStreamBitrateNotification:
                        Debug( "-TempMaxMediaStreamBitrateNotification received from [%s] on %s stream\n", fromAddr, MediaFrame::TypeToString( media ) );
                        pendingTMBR = false;
                        if( requestFPU )
                        {
                            requestFPU = false;
                            DWORD ssrc = defaultStream->GetRecSSRC();
                            SendFIR( ssrc );
                        }
                        for( BYTE i = 0; i < fb->GetFieldCount(); i++ )
                        {
                            //Get field
                            RTCPRTPFeedback::TempMaxMediaStreamBitrateField *field = (RTCPRTPFeedback::TempMaxMediaStreamBitrateField *)fb->GetField( i );
                            Debug( "-TempMaxMediaStreamBitrateNotification: maxBitrate = %d, overhead=%d\n", field->GetBitrate(), field->GetOverhead() );
                        }
                        break;
                }
                break;
            }
            case RTCPPacket::PayloadFeedback:
            {
                //Get feedback packet
                RTCPPayloadFeedback *fb = (RTCPPayloadFeedback *)packet;
                //Check feedback type
                switch( fb->GetFeedbackType() )
                {
                    case RTCPPayloadFeedback::PictureLossIndication:
                    case RTCPPayloadFeedback::FullIntraRequest:
                        //Chec listener
                        if( listener )
                            //Send intra refresh
                            listener->onFPURequested( this );
                        break;
                    case RTCPPayloadFeedback::SliceLossIndication:
                        Log( "-RTCP SliceLossIndication received\n" );
                        break;
                    case RTCPPayloadFeedback::ReferencePictureSelectionIndication:
                        Log( "-RTCP ReferencePictureSelectionIndication  received\n" );
                        break;
                    case RTCPPayloadFeedback::TemporalSpatialTradeOffRequest:
                        Log( "-RTCP TemporalSpatialTradeOffRequest  received\n" );
                        break;
                    case RTCPPayloadFeedback::TemporalSpatialTradeOffNotification:
                        Log( "-RTCP TemporalSpatialTradeOffNotification\n" );
                        break;
                    case RTCPPayloadFeedback::VideoBackChannelMessage:
                        Log( "-RTCP VideoBackChannelMessage\n" );
                        break;
                    case RTCPPayloadFeedback::ApplicationLayerFeeedbackMessage:
                        for( BYTE i = 0; i < fb->GetFieldCount(); i++ )
                        {
                            //Get feedback
                            RTCPPayloadFeedback::ApplicationLayerFeeedbackField *msg = (RTCPPayloadFeedback::ApplicationLayerFeeedbackField *)fb->GetField( i );
                            //Get size and payload
                            DWORD len = msg->GetLength();
                            BYTE *payload = msg->GetPayload();
                            //Check if it is a REMB
                            if( len > 8 && payload[0] == 'R' && payload[1] == 'E' && payload[2] == 'M' && payload[3] == 'B' )
                            {
                                //GEt exponent
                                BYTE exp = payload[5] >> 2;
                                DWORD mantisa = payload[5] & 0x03;
                                mantisa = mantisa << 8 | payload[6];
                                mantisa = mantisa << 8 | payload[7];
                                //Get bitrate
                                DWORD bitrate = mantisa << exp;
                                //Check if it is for us
                                if( listener )
                                    //call listener
                                    listener->onReceiverEstimatedMaxBitrate( this, bitrate );
                            }
                        }
                        break;
                }
                break;
            }
            case RTCPPacket::FullIntraRequest:
                //This is message deprecated and just for H261, but just in case
                //Check listener
                if( listener )
                    //Send intra refresh
                    listener->onFPURequested( this );
                break;
            case RTCPPacket::NACK:
                break;
        }
    }
    //Delete pacekt
    delete(rtcp);
}

int RTPSession::SendFIR( DWORD &ssrc )
{
    Log( "-SendFIR\n" );

    //Create rtcp sender report
    RTCPCompoundPacket *rtcp = CreateSenderReport();

    DWORD recSSRC = ssrc;
    if( getStream( ssrc ) == NULL && recSSRC == 0 && defaultStream != NULL )
        recSSRC = defaultStream->GetRecSSRC();

    //Create fir request
    RTCPPayloadFeedback *fir = RTCPPayloadFeedback::Create( RTCPPayloadFeedback::FullIntraRequest, sendSSRC, recSSRC );
    //ADD field
    fir->AddField( new RTCPPayloadFeedback::FullIntraRequestField( recSSRC, firReqNum++ ) );
    //Add to rtcp
    rtcp->AddRTCPacket( fir );

    //Add PLI
    RTCPPayloadFeedback *pli = RTCPPayloadFeedback::Create( RTCPPayloadFeedback::PictureLossIndication, sendSSRC, recSSRC );
    //Add to rtcp
    rtcp->AddRTCPacket( pli );

    //Send packet
    int ret = SendPacket( *rtcp );

    //Delete it
    delete(rtcp);

    return ret;
}

int RTPSession::RequestFPU()
{
    if( defaultStream == NULL )
        return 0;

    DWORD ssrc = defaultStream->GetRecSSRC();
    return RequestFPU( ssrc );
}

int RTPSession::RequestFPU( DWORD &ssrc )
{
    //Send all the packets inmediatelly to the decoder so I frame can be handled as soon as possible
    RTPStream *stream = getStream( ssrc );
    if( stream == NULL )
        stream = defaultStream;

    if( stream != NULL ) stream->HurryUp();
    ssrc = stream->GetRecSSRC();
    //request FIR
    SendFIR( ssrc );

    //packets.Reset();
    /*if (!pendingTMBR)
    {
        //request FIR
        SendFIR();
    } else {
        //Wait for TMBN response to no overflow
        requestFPU = true;
    }*/
}

void RTPSession::SetRTT( DWORD rtt )
{
    //Set it
    this->rtt = rtt;
    DWORD recSSRC = 0;
    if( defaultStream != NULL )
        recSSRC = defaultStream->GetRecSSRC();
    else
        return;

    //if got estimator
    if( remoteRateEstimator )
    {
        //Update estimator
        remoteRateEstimator->UpdateRTT( recSSRC, rtt, getTimeMS() );
    }

    //Check RTT to enable NACK
    if( useNACK && rtt < 240 )
    {
        //Enable NACK only if RTT is small
        isNACKEnabled = true;
        //Update jitter buffer size in ms in [60+rtt,300]
        defaultStream->SetMaxWaitTime( fmin( 60 + rtt, 300 ) );
    }
    else
    {
        //Disable NACK
        isNACKEnabled = false;
        //Reduce jitter buffer as we don't use NACK
        defaultStream->SetMaxWaitTime( 60 );
    }
}

void RTPSession::onTargetBitrateRequested( DWORD bitrate )
{
    bool fb;

    // Memory barrier
    mutex.lock();
    fb = sendBitrateFeedback;
    mutex.unlock();
    Debug( "-RTPSession::onTargetBitrateRequested() %i, bitrate [%d] for %s stream %p.\n", fb, bitrate, MediaFrame::TypeToString( media ), this );
    if( fb )
    {
        Debug( "-RTPSession::onTargetBitrateRequested() sending TMMBR\n", bitrate );
        //Create rtcp sender retpor
        RTCPCompoundPacket *rtcp = CreateSenderReport();

        DWORD recSSRC = 0;
        if( defaultStream != NULL )
            recSSRC = defaultStream->GetRecSSRC();

        //Create TMMBR
        RTCPRTPFeedback *rfb = RTCPRTPFeedback::Create( RTCPRTPFeedback::TempMaxMediaStreamBitrateRequest, sendSSRC, recSSRC );
        //Limit incoming bitrate
        rfb->AddField( new RTCPRTPFeedback::TempMaxMediaStreamBitrateField( recSSRC, bitrate, 0 ) );
        //Add to packet
        rtcp->AddRTCPacket( rfb );

        //We have a pending request
        pendingTMBR = true;
        //Store values
        pendingTMBBitrate = bitrate;

        //Send packet
        SendPacket( *rtcp );

        //Delete it
        delete(rtcp);
    }
}

void RTPSession::ReSendPacket( int seq )
{
    DWORD recCycles = 0;

    //Lock
    if( !useNACK )
    {
        Debug( "Asked to resend RTP seq %d but NACK not enabled.\n", seq );
        return;
    }

    rtxUse.IncUse();
    if( defaultStream != NULL )
        recCycles = defaultStream->GetRecCycles();

    //Calculate ext seq number
    DWORD ext = ((DWORD)recCycles) << 16 | seq;

    //Find packet to retransmit
    RTPOrderedPackets::iterator it = rtxs.find( ext );

    //If we still have it
    if( it != rtxs.end() )
    {
        //Get packet
        DWORD size = MTU;
        BYTE data[MTU + SRTP_MAX_TRAILER_LEN];

        RTPTimedPacket *packet = it->second;
        int len = packet->GetSize();

        if( len > size )
        {
            //Error
            Error( "-RTPSession::ReSendPacket() | not enougth size for copying packet [len:%d]\n", len );
            return;
        }

        //Copy
        memcpy( data, packet->GetData(), packet->GetSize() );
#if 0	
        // Does not work - recencryption fails
       //If using abs-time
        if( useAbsTime )
        {
            //Calculate absolute send time field (convert ms to 24-bit unsigned with 18 bit fractional part.
            // Encoding: Timestamp is in seconds, 24 bit 6.18 fixed point, yielding 64s wraparound and 3.8us resolution (one increment for each 477 bytes going out on a 1Gbps interface).
            DWORD abs = ((getTimeMS() << 18) / 1000) & 0x00ffffff;
            //Overwrite it
            set3( data, sizeof( rtp_hdr_t ) + sizeof( rtp_hdr_ext_t ) + 1, abs );
        }

        //Check if we ar encripted
        if( encript )
        {
            //Check  session
            if( !sendSRTPSession )
            {
                //Error
                Error( "-RTPSession::ReSendPacket() | no sendSRTPSession\n" );
                return;
            }
            //Encript
            err_status_t err = srtp_protect( sendSRTPSession, data, &len );
            //Check error
            if( err != err_status_ok )
            {
                //Check if got listener
                if( listener )
                    //Request a I frame
                    listener->onFPURequested( this );

                //Nothing
                Error( "-RTPSession::ReSendPacket() | Error protecting RTP packet [%d] sending intra instead\n", err );
                return;
            }
        }
        //Check len
#endif
        if( len )
        {
            Debug( "-resent nacked packet ext %d seq %d rtpsess=%p.\n", ext, packet->GetSeqNum(), this );
            //Send packet
            sendto( simSocket, data, len, 0, (sockaddr *)&sendAddr, sizeof( struct sockaddr_in ) );
        }
    }
    else
    {
        it = rtxs.begin();
        int first = (it == rtxs.end()) ? 0 : it->first;

        Debug( "-could not resent nacket packet seq %d: not in buffer anymore. first seq=%d, cout=%d, rtpsess=%p useNacl=%d\n",
            ext, first, rtxs.size(), this, useNACK );

        //Check if got listener
        if( listener )
            //Request a I frame
            listener->onFPURequested( this );
    }

    //Unlock
    rtxUse.DecUse();
}

int RTPSession::SendTempMaxMediaStreamBitrateNotification( DWORD bitrate, DWORD overhead )
{
    Log( "-SendTempMaxMediaStreamBitrateNotification [%d,%d]\n", bitrate, overhead );

    //Create rtcp sender retpor
    RTCPCompoundPacket *rtcp = CreateSenderReport();

    DWORD recSSRC = 0;
    if( defaultStream != NULL )
        recSSRC = defaultStream->GetRecSSRC();

    //Create TMMBR
    RTCPRTPFeedback *rfb = RTCPRTPFeedback::Create( RTCPRTPFeedback::TempMaxMediaStreamBitrateNotification, sendSSRC, recSSRC );
    //Limit incoming bitrate
    rfb->AddField( new RTCPRTPFeedback::TempMaxMediaStreamBitrateField( sendSSRC, bitrate, 0 ) );
    //Add to packet
    rtcp->AddRTCPacket( rfb );

    //Send packet
    int ret = SendPacket( *rtcp );

    //Delete it
    delete(rtcp);

    //Exit
    return ret;
}


/**
 *  Create a new RTP stream. If the stream already exists, it does nothing
 *
 *  @param receiving: if this is a receiving or sending stream (only receiving is supported at the moment)
 *  @param ssrc: ssrc of this new stream.
 */
bool RTPSession::AddStream( bool receiving, DWORD ssrc )
{
    if( streamUse.WaitUnusedAndLock( 500 ) )
    {
        RTPStream *stream = getStream( ssrc );
        if( stream == NULL )
        {
            stream = new RTPStream( this, ssrc );

            streams[ssrc] = stream;
            //If remote estimator
            if( remoteRateEstimator )
                //Add stream
                remoteRateEstimator->AddStream( ssrc );
        }
        streamUse.Unlock();
        return true;
    }
    else
    {
        return false;
    }
}

bool RTPSession::DeleteStreams()
{
    streamUse.IncUse();
    for( Streams::iterator it = streams.begin(); it != streams.end(); it++ )
    {
        it->second->disabled = true;
        it->second->Cancel();
    }
    streamUse.DecUse();

    streamUse.WaitUnusedAndLock();

    for( Streams::iterator it = streams.begin(); it != streams.end(); it++ )
    {
        if( remoteRateEstimator )
            //Add stream
            remoteRateEstimator->RemoveStream( it->first );
        delete it->second;
    }

    streams.clear();

    defaultStream = NULL;
    streamUse.Unlock();
    return true;
}
/**
 * Set the stream designated by SSRC as the defaut stream, if the stream does not exist create it
 *
 * @param: receiving whether it is receving or sending default stream
 * @param: ssrc: ssrc of the stream to be set as default
 *
 **/

bool RTPSession::SetDefaultStream( bool receiving, DWORD ssrc )
{
    AddStream( receiving, ssrc );
    defaultStream = getStream( ssrc );

    return true;
}

/**
 *  get the stream associated to the SSRC .
 */
RTPSession::RTPStream *RTPSession::getStream( DWORD ssrc )
{
    //Find stream
    Streams::iterator it = streams.find( ssrc );

    //If not found
    if( it == streams.end() )
        //Error
        return NULL;

    //Get the stream
    return it->second;
}

/**
 *  Change the SSRC of an existing stream.
 */
bool RTPSession::ChangeStream( DWORD oldssrc, DWORD newssrc )
{
    RTPStream *stream = getStream( oldssrc );
    streams.erase( oldssrc );
    stream->SetRecSSRC( newssrc );
    streams[newssrc] = stream;

    return true;
}

RTCPCompoundPacket *RTPSession::CreateSenderReport()
{
    timeval tv;

    //Create packet
    RTCPCompoundPacket *rtcp = new RTCPCompoundPacket();

    //Get now
    gettimeofday( &tv, NULL );

    //Create Sender report
    RTCPSenderReport *sr = new RTCPSenderReport();

    //Append data
    sr->SetSSRC( sendSSRC );
    sr->SetTimestamp( &tv );
    sr->SetRtpTimestamp( sendLastTime );
    sr->SetOctectsSent( totalSendBytes );
    sr->SetPacketsSent( numSendPackets );

    //Update time of latest sr
    DWORD sinceLastSR = getUpdDifTime( &lastSR );


    for( Streams::iterator it = streams.begin(); it != streams.end(); it++ )
    {
        RTCPReport *report = NULL;

        if( it->second )
        {
            //Create report
            report = it->second->CreateReceiverReport();
        }

        if( report != NULL )
        {
            //Append it
            sr->AddReport( report );
        }

    }


    //Append SR to rtcp
    rtcp->AddRTCPacket( sr );

    //Store last send SR 32 middle bits
    SetSendSR( sr->GetNTPSec() << 16 | sr->GetNTPFrac() >> 16 );

    //Create SDES
    RTCPSDES *sdes = new RTCPSDES();

    //Create description
    RTCPSDES::Description *desc = new RTCPSDES::Description();
    //Set ssrc
    desc->SetSSRC( sendSSRC );
    //Add item
    desc->AddItem( new RTCPSDES::Item( RTCPSDES::Item::CName, cname ) );

    //Add it
    sdes->AddDescription( desc );

    //Add to rtcp
    rtcp->AddRTCPacket( sdes );

    //Return it
    return rtcp;
}

int RTPSession::SendSenderReport()
{
    //Create rtcp sender retpor
    RTCPCompoundPacket *rtcp = CreateSenderReport();
    DWORD recSSRC = 0;
    if( defaultStream != NULL )
        recSSRC = defaultStream->GetRecSSRC();

    //If we have not got a notification from latest TMBR
    if( pendingTMBR )
    {
        //Resend TMMBR
        RTCPRTPFeedback *rfb = RTCPRTPFeedback::Create( RTCPRTPFeedback::TempMaxMediaStreamBitrateRequest, sendSSRC, recSSRC );
        //Limit incoming bitrate
        rfb->AddField( new RTCPRTPFeedback::TempMaxMediaStreamBitrateField( recSSRC, pendingTMBBitrate, 0 ) );
        //Add to packet
        rtcp->AddRTCPacket( rfb );
    }
    else if( remoteRateEstimator && sendBitrateFeedback )
    {
        //Get lastest estimation and convert to kbps
        DWORD estimation = remoteRateEstimator->GetEstimatedBitrate();
        //If it was ok
        if( estimation )
        {
            //Resend TMMBR
            RTCPRTPFeedback *rfb = RTCPRTPFeedback::Create( RTCPRTPFeedback::TempMaxMediaStreamBitrateRequest, sendSSRC, recSSRC );
            //Limit incoming bitrate
            rfb->AddField( new RTCPRTPFeedback::TempMaxMediaStreamBitrateField( recSSRC, estimation, 0 ) );
            //Add to packet
            rtcp->AddRTCPacket( rfb );
            std::list<DWORD> ssrcs;
            //Get ssrcs
            remoteRateEstimator->GetSSRCs( ssrcs );
            //Create feedback
            // SSRC of media source (32 bits):  Always 0; this is the same convention as in [RFC5104] section 4.2.2.2 (TMMBN).
            RTCPPayloadFeedback *remb = RTCPPayloadFeedback::Create( RTCPPayloadFeedback::ApplicationLayerFeeedbackMessage, sendSSRC, 0 );
            //Send estimation
            remb->AddField( RTCPPayloadFeedback::ApplicationLayerFeeedbackField::CreateReceiverEstimatedMaxBitrate( ssrcs, estimation ) );
            //Add to packet
            rtcp->AddRTCPacket( remb );
            Debug( "SR: reporting estimated bandwidth of %d to %s\n", estimation, inet_ntoa( sendRtcpAddr.sin_addr ) );
        }
    }

    //Send packet
    int ret = SendPacket( *rtcp );

    //Delete it
    delete(rtcp);

    //Exit
    return ret;
}


// Default behavior
void RTPSession::Listener::onNewStream( RTPSession *session, DWORD newSsrc, bool receiving )
{
    if( !receiving ) return;


    DWORD oldssrc = session->GetDefaultStream( true );

    if( oldssrc )
    {
        session->ChangeStream( oldssrc, newSsrc );
    }
    else
    {
        session->SetDefaultStream( true, newSsrc );
    }
}

bool RTPSession::RTPStream::Add( RTPTimedPacket *packet, DWORD size )
{
    //Get sec number
    WORD seq = packet->GetSeqNum();

    //Check if we have a sequence wrap
    if( seq == 0 )
    {
        Log( "-RTPSession: we should have a seq wrap. lastSeq=0x%0x, current cycle = %d\n", recExtSeq, recCycles );
    }

    if( seq < 0x00FF && (recExtSeq & 0xFFFF) > 0xFF00U )
    {
        //Increase cycles
        recCycles++;
        Log( "-RTPSession: seqno wrap. lastSeq=0x%0x, NEW cycle = %d\n", recExtSeq, recCycles );
    }
    //Set cycles
    packet->SetSeqCycles( recCycles );

    //If remote estimator
    if( s->GetRemoteRateEstimator() )
        //Update rate control
        s->GetRemoteRateEstimator()->Update( recSSRC, packet, getTimeMS() );

    //Increase stats
    numRecvPackets++;
    totalRecvPacketsSinceLastSR++;
    totalRecvBytes += size;
    totalRecvBytesSinceLastSR += size;
    recCodec = packet->GetCodec();

    //Get ext seq
    DWORD extSeq = packet->GetExtSeqNum();

    //Check if it is the min for this SR
    if( extSeq < minRecvExtSeqNumSinceLastSR )
        //Store minimum
        minRecvExtSeqNumSinceLastSR = extSeq;

    //If we have a not out of order pacekt
    if( extSeq > recExtSeq )
    {
        //Check possible lost packets
        if( recExtSeq && extSeq > (recExtSeq + 1) )
        {
            //Get number of lost packets
            WORD lost = extSeq - recExtSeq - 1;
            //Base packet missing
            WORD base = recExtSeq + 1;

            //If remote estimator
            if( s->GetRemoteRateEstimator() )
                //Update estimator
                s->GetRemoteRateEstimator()->UpdateLost( recSSRC, lost, size );

            //If nack is enable t waiting for a PLI/FIR response (to not oeverflow)
            if( s->IsNACKEnabled() && !s->IsRequestFPU() )
            {
                //Double check
                if( lost < 0x0FFF )
                {
                    Log( "-Nacking %d lost %d\n", base, lost );

                    //Generate new RTCP NACK report
                    RTCPCompoundPacket *rtcp = new RTCPCompoundPacket();

                    //Send them
                    while( lost > 0 )
                    {
                        //Skip base
                        lost--;
                        //Get number of lost in the 16 mask
                        WORD n = lost;
                        //Check nex 16 packets
                        if( n > 16 )
                            //Clip
                            n = 16;
                        //Create mask
                        WORD mask = 0xFFFF << (16 - n);
                        //Create NACK
                        RTCPRTPFeedback *nack = RTCPRTPFeedback::Create( RTCPRTPFeedback::NACK, s->GetSendSSRC(), recSSRC );
                        //Limit incoming bitrate
                        nack->AddField( new RTCPRTPFeedback::NACKField( base, mask ) );
                        //Add to packet
                        rtcp->AddRTCPacket( nack );
                        //Reduce lost
                        lost -= n;
                        //Increase base
                        base += 16;
                    }

                    //Send packet
                    s->SendPacket( *rtcp );

                    //Delete it
                    delete(rtcp);
                }
                else
                {
                    Error( "-Weird lost count [lost:%d,base:%d,recExtSeq:%d,recCycles:%d,extSeq:%d,seq:%d]\n"
                        , lost, base, recExtSeq, recCycles, extSeq, packet->GetSeqNum() );
                }
            }
        }

        //Update seq num
        recExtSeq = extSeq;

        //Get diff from previous
        DWORD diff = getUpdDifTime( &recTimeval ) / 1000;

        //If it is not first one and not from the same frame
        if( recTimestamp && recTimestamp < packet->GetClockTimestamp() )
        {
            //Get difference of arravail times
            int d = (packet->GetClockTimestamp() - recTimestamp) - diff;
            //Check negative
            if( d < 0 )
                //Calc abs
                d = -d;
            //Calculate variance
            int v = d - jitter;
            //Calculate jitter
            jitter += v / 16;
        }

        //Update rtp timestamp
        recTimestamp = packet->GetClockTimestamp();
    }

    //Check if we are using fec
    if( s->UseFEC() )
    {
        //Append to the FEC decoder
        if( fec.AddPacket( packet ) )
            //Append only packets with media data
            RTPBuffer::Add( packet );
        //Try to recover
        RTPTimedPacket *recovered = fec.Recover();
        //If we have recovered a pacekt
        while( recovered )
        {
            //Log
            Log( "packet recovered!!!!\n" );
            //Overwrite time with the time of the original 
            recovered->SetTime( packet->GetTime() );
            //Get pacekte type
            BYTE t = recovered->GetType();
            //Map receovered data codec
            BYTE c = s->GetRtpMapIn()->GetCodecForType( t );
            //Check codec
            if( c != RTPMap::NotFound )
                //Set codec
                recovered->SetCodec( c );
            else
                //Set type for passtrhought
                recovered->SetCodec( t );
            //add recovered packet
            RTPBuffer::Add( recovered );
            //Try to recover another one (yuhu!)
            recovered = fec.Recover();
        }
    }
    else
    {
        //Add packet
        RTPBuffer::Add( packet );
    }
    timeval lastSR = s->GetLastSR();

    //Check if we need to send SR
    if( isZeroTime( &lastSR ) || getDifTime( &lastSR ) > 2000000 )
        //Send it
        s->SendSenderReport();
}

RTCPReport *RTPSession::RTPStream::CreateReceiverReport()
{
    //Create report
    RTCPReport *report = NULL;
    //If we have received somthing
    timeval lastReceivedSR = s->GetLastReceivedSR();

    if( totalRecvPacketsSinceLastSR && recExtSeq >= minRecvExtSeqNumSinceLastSR )
    {
        //Get number of total packtes
        DWORD total = recExtSeq - minRecvExtSeqNumSinceLastSR + 1;
        //Calculate lost
        DWORD lostRecvPacketsSinceLastSR = total - totalRecvPacketsSinceLastSR;
        //Add to total lost count
        lostRecvPackets += lostRecvPacketsSinceLastSR;
        //Calculate fraction lost
        DWORD frac = (lostRecvPacketsSinceLastSR * 256) / total;

        //Create report
        report = new RTCPReport();

        //Set SSRC of incoming rtp stream
        report->SetSSRC( recSSRC );

        //Check last report time
        if( !isZeroTime( &lastReceivedSR ) )
            //Get time and update it
            report->SetDelaySinceLastSRMilis( getDifTime( &lastReceivedSR ) / 1000 );
        else
            //No previous SR
            report->SetDelaySinceLastSRMilis( 0 );
        //Set data
        report->SetLastSR( s->GetRecSR() );
        report->SetFractionLost( frac );
        report->SetLastJitter( jitter );
        report->SetLostCount( lostRecvPackets );
        report->SetLastSeqNum( recExtSeq );

        //Reset data
        totalRecvPacketsSinceLastSR = 0;
        totalRecvBytesSinceLastSR = 0;
        minRecvExtSeqNumSinceLastSR = RTPPacket::MaxExtSeqNum;
    }

    return report;
}

void RTPSession::onDTLSSetup( DTLSConnection::Suite suite, BYTE *localMasterKey, DWORD localMasterKeySize, BYTE *remoteMasterKey, DWORD remoteMasterKeySize )
{
    Log( "-onDTLSSetup for [%s]\n", MediaFrame::TypeToString( media ) );

    switch( suite )
    {
        case DTLSConnection::AES_CM_128_HMAC_SHA1_80:
            //Set keys
            SetLocalCryptoSDES( "AES_CM_128_HMAC_SHA1_80", localMasterKey, localMasterKeySize );
            SetRemoteCryptoSDES( "AES_CM_128_HMAC_SHA1_80", remoteMasterKey, remoteMasterKeySize, 0 );
            break;
        case DTLSConnection::AES_CM_128_HMAC_SHA1_32:
            //Set keys
            SetLocalCryptoSDES( "AES_CM_128_HMAC_SHA1_32", localMasterKey, localMasterKeySize );
            SetRemoteCryptoSDES( "AES_CM_128_HMAC_SHA1_32", remoteMasterKey, remoteMasterKeySize, 0 );
            break;
        case DTLSConnection::F8_128_HMAC_SHA1_80:
            //Set keys
            SetLocalCryptoSDES( "NULL_CIPHER_HMAC_SHA1_80", localMasterKey, localMasterKeySize );
            SetRemoteCryptoSDES( "NULL_CIPHER_HMAC_SHA1_80", remoteMasterKey, remoteMasterKeySize, 0 );
            break;
    }
}

bool RTPSession::GetStatistics( DWORD ssrc, MediaStatistics &stats )
{
    memset( &stats, 0, sizeof( stats ) );

    if( !running || rtpMapOut == NULL )
    {
        return false;
    }

    stats.numSendPackets = numSendPackets;
    stats.totalSendBytes = totalSendBytes;

    if( rtpMapOut->find( sendType ) != rtpMapOut->end() )
        stats.sendingCodec = (*rtpMapOut)[sendType];
    else
        stats.sendingCodec = -1;

    RTPStream *s = (ssrc > 0) ? getStream( ssrc ) : NULL;
    if( s != NULL )
    {
        stats.numRecvPackets = s->GetNumRecvPackets();
        stats.totalRecvBytes = s->GetTotalRecvBytes();
        stats.bwIn = 0;
        stats.lostRecvPackets = s->GetLostRecvPackets();
        stats.receivingCodec = s->GetRecCodec();
    }
    else
    {
        for( Streams::iterator it = streams.begin(); it != streams.end(); it++ )
        {
            s = it->second;
            if( s != NULL )
            {
                stats.numRecvPackets += s->GetNumRecvPackets();
                stats.totalRecvBytes += s->GetTotalRecvBytes();
                stats.bwIn = 0;
                stats.lostRecvPackets += s->GetLostRecvPackets();
            }
        }
        if( defaultStream != NULL )
            stats.receivingCodec = defaultStream->GetRecCodec();
    }

    return true;
}

