#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>
#include "textstream.h"
#include "log.h"
#include "tools.h"

static BYTE BOMUTF8[]			= {0xEF,0xBB,0xBF};
static BYTE LOSTREPLACEMENT[]		= {0xEF,0xBF,0xBD};

/**********************************
* TextStream
*	Constructor
***********************************/
TextStream::TextStream(RTPSession::Listener* listener) : rtp(MediaFrame::Text,listener)
{
	sendingText=TaskIdle;
	receivingText=TaskIdle;
	textCodec=TextCodec::T140;
	muted = false;
}

/*******************************
* ~TextStream
*	Destructor. 
********************************/
TextStream::~TextStream()
{
}

/***************************************
* SetTextCodec
*	Fija el codec de text
***************************************/
int TextStream::SetTextCodec(TextCodec::Type codec)
{
	//Compromabos que soportamos el modo
	if (!(codec==TextCodec::T140 || codec==TextCodec::T140RED))
		return 0;

	//Colocamos el tipo de text
	textCodec = codec;

	Log("-SetTextCodec [%d,%s]\n",textCodec,TextCodec::GetNameFor(textCodec));

	//Y salimos
	return 1;	
}

/***************************************
* Init
*	Inicializa los devices 
***************************************/
int TextStream::Init(TextInput *input, TextOutput *output)
{
	Log(">Init text stream\n");

	//Iniciamos el rtp
	if(!rtp.Init())
		return Error("No hemos podido abrir el rtp\n");
	

	//Nos quedamos con los puntericos
	textInput  = input;
	textOutput = output;

	//Y aun no estamos mandando nada
	sendingText=TaskIdle;
	receivingText=TaskIdle;

	Log("<Init text stream\n");

	return 1;
}

int TextStream::SetLocalCryptoSDES(const char* suite, const char* key64)
{
	return rtp.SetLocalCryptoSDES(suite,key64);
}

int TextStream::SetRemoteCryptoSDES(const char* suite, const char* key64)
{
	return rtp.SetRemoteCryptoSDES(suite,key64);
}

int TextStream::SetRemoteCryptoDTLS(const char *setup,const char *hash,const char *fingerprint)
{
	return rtp.SetRemoteCryptoDTLS(setup, hash, fingerprint);
}


int TextStream::SetLocalSTUNCredentials(const char* username, const char* pwd)
{
	return rtp.SetLocalSTUNCredentials(username,pwd);
}

int TextStream::SetRemoteSTUNCredentials(const char* username, const char* pwd)
{
	return rtp.SetRemoteSTUNCredentials(username,pwd);
}
int TextStream::SetRTPProperties(const Properties& properties)
{
	return rtp.SetProperties(properties);
}
/***************************************
* startSendingText
*	Helper function
***************************************/
void * TextStream::startSendingText(void *par)
{
	TextStream *conf = (TextStream *)par;
	blocksignals();
	Log("SendTextThread [%d]\n",getpid());
	pthread_exit((void *)conf->SendText());
}

/***************************************
* startReceivingText
*	Helper function
***************************************/
void * TextStream::startReceivingText(void *par)
{
	TextStream *conf = (TextStream *)par;
	blocksignals();
	Log("RecvTextThread [%d]\n",getpid());
	pthread_exit((void *)conf->RecText());
}

/***************************************
* StartSending
*	Comienza a mandar a la ip y puertos especificados
***************************************/
int TextStream::StartSending(char *sendTextIp,int sendTextPort,RTPMap& rtpMap)
{
	Log(">StartSending text [%s,%d]\n",sendTextIp,sendTextPort);

	//Si estabamos mandando tenemos que parar
	StopSending();
	
	if (sendingText != TaskIdle)
		return Error("Cannot start sending text: bad state.\n");
	
	//Si tenemos text
	if (sendTextPort==0)
		//Error
		return Error("Text port 0\n");


	//Y la de text
	if(!rtp.SetRemotePort(sendTextIp,sendTextPort))
		//Error
		return Error("Error en el SetRemotePort\n");

	//Set sending map
	rtp.SetSendingRTPMap(rtpMap);

	//Get t140 for redundancy
	for (RTPMap::iterator it = rtpMap.begin(); it!=rtpMap.end(); ++it)
	{
		//Is it ourr codec
		if (it->second==TextCodec::T140)
		{
			//Set it
			t140Codec = it->first;
			//and we are done
			continue;
		}
	}

	//Set text codec
	if(!rtp.SetSendingCodec(textCodec))
		//Error
		return Error("%s text codec not supported by peer\n",TextCodec::GetNameFor(textCodec));

	//Estamos mandando
	sendingText = TaskStarting;

	//Start thread
	createPriorityThread(&sendTextThread,startSendingText,this,1);

	Log("<StartSending text [%d]\n",sendingText);

	return 1;
}

/***************************************
* StartReceiving
*	Abre los sockets y empieza la recetpcion
****************************************/
int TextStream::StartReceiving(RTPMap& rtpMap)
{
	//If already receiving
	StopReceiving();
	
	if (receivingText != TaskIdle)
		return Error("Failed to start receiving text. Task in bad state.\n");

	
	//Get local rtp port
	int recTextPort = rtp.GetLocalPort();

	//Set receving map
	rtp.SetReceivingRTPMap(rtpMap);

	//Estamos recibiendo
	receivingText= TaskStarting;

	//Create thread
	createPriorityThread(&recTextThread,startReceivingText,this,1);

	//Log
	Log("<StartReceiving text [%d]\n",recTextPort);

	//Return receiving port
	return recTextPort;
}

/***************************************
* End
*	Termina la conferencia activa
***************************************/
int TextStream::End()
{
	//Terminamos de enviar
	StopSending();

	//Y de recivir
	StopReceiving();

	//Cerramos la session de rtp
	rtp.End();

	return 1;
}

/***************************************
* StopReceiving
* 	Termina la recepcion
****************************************/

int TextStream::StopReceiving()
{
	Log(">StopReceiving Text\n");

	//Y esperamos a que se cierren las threads de recepcion
	if (receivingText  == TaskRunning || receivingText  ==TaskStarting)
	{	
		//Dejamos de recivir
		receivingText = TaskStopping;

		//Cancel rtp
		rtp.CancelGetPacket();
		
		//Y unimos
		pthread_join(recTextThread,NULL);
	}

	Log("<StopReceiving Text\n");

	return 1;

}

/***************************************
* StopSending
* 	Termina el envio
****************************************/
int TextStream::StopSending()
{
	Log(">StopSending Text\n");

	//Esperamos a que se cierren las threads de envio
	if (sendingText == TaskRunning || sendingText == TaskStarting)
	{
		//Paramos el envio
		sendingText = TaskStopping;

		//Cancel grab if any
		textInput->Cancel();

		//Y esperamos
		pthread_join(sendTextThread,NULL);
	}

	Log("<StopSending Text\n");

	return 1;	
}


/****************************************
* RecText
*	Obtiene los packetes y los muestra
*****************************************/
int TextStream::RecText()
{
	BYTE*		text;
	DWORD		textSize;
	DWORD		lastSeq = RTPPacket::MaxExtSeqNum;

	Log(">RecText\n");
	rtp.ResetPacket(false);
	receivingText = TaskRunning;
	//Mientras tengamos que capturar
	while(receivingText  == TaskRunning)
	{
		//Get packet
		RTPPacket* packet = rtp.GetPacket();

		//Check if gor anti
		if (!packet)
                {
			//Next
                        msleep(200);
			continue;
                }

		//Get type
		TextCodec::Type type = (TextCodec::Type)packet->GetCodec();

		//Get timestamp
		DWORD timeStamp = packet->GetTimestamp();

		//Get extended sequence number
		DWORD seq = packet->GetExtSeqNum();

		//Lost packets since last one
		DWORD lost = 0;

		//If not first
		if (lastSeq!=RTPPacket::MaxExtSeqNum)
			//Calculate losts
			lost = seq-lastSeq-1;

		//Update last sequence number
		lastSeq = seq;

		//Check if we are muted
		if (!muted)
		{
			//Check the type of data
			if (type==TextCodec::T140RED)
			{
				//Get redundant packet
				RTPRedundantPacket* red = (RTPRedundantPacket*) packet;				
                                redCodec.Decode(red, textOutput);
			} 
                        else {
				//For each lost packet send a mark
				for (int i=0;i<lost;i++)
				{
					//Create frame of lost replacement
					TextFrame frame(timeStamp,LOSTREPLACEMENT,sizeof(LOSTREPLACEMENT));
					//Y lo reproducimos
					textOutput->SendFrame(frame);
				}
				//Create frame
				TextFrame frame(timeStamp,packet->GetMediaData(),packet->GetMediaLength());
				//Send it
				textOutput->SendFrame(frame);
			}
		}

		//Delete rtp packet
		delete(packet);
	}

	Log("<RecText\n");

	receivingText = TaskIdle;
	//Salimos
	pthread_exit(0);
}

/*******************************************
* SendText
*	Capturamos el text y lo mandamos
*******************************************/
int TextStream::SendText()
{
    RTPPacket packet(MediaFrame::Text,textCodec);
    bool idle = true;
    DWORD timeout = 25000;
    DWORD lastTime = 0;

    Log(">SendText\n");
    sendingText = TaskRunning;
    //Mientras tengamos que capturar
    while(sendingText == TaskRunning)
    {
        //Text frame
        TextFrame *frame = NULL;

        //Get frame
        frame = textInput->GetFrame(timeout);

        //Calculate last frame time
        if (frame)
            //Get it from frame
            lastTime = frame->GetTimeStamp();
        else
	{
	    msleep(200);
            //Update last send time with timeout
            lastTime += timeout;
	}

        //Check codec
        if (textCodec == TextCodec::T140) 
        {
            //Check frame
            if (frame) {
                //Set timestamp
                packet.SetTimestamp(lastTime);
                //Set data
                packet.SetPayload(frame->GetData(), frame->GetLength());

                //Set Mark for the first frame after idle
                packet.SetMark(idle);

                //Send it
                rtp.SendPacket(packet);

                //Delete frame
                delete(frame);

                //Not idle anymore
                idle = false;
            } 
            else {
                //Set data
                packet.SetPayload(BOMUTF8, sizeof (BOMUTF8));

                //No mark
                packet.SetMark(false);

                //Send it
                rtp.SendPacket(packet);
            }
        } 
        else {
            // Redundent text
            RTPRedundantPacket *redpak = NULL;
            
            if (frame) {
                redpak = redCodec.Encode(frame, t140Codec);
            }
            else 
            {
                if (idle) 
                {
                    redpak = redCodec.EncodeBOM(t140Codec);
                } 
                else 
                {
                    redpak = redCodec.EncodeNull(t140Codec);
                }
				if (redpak)
					redpak->SetTimestamp(lastTime);
            }
            //Send the mark bit if it is first frame after idle
            bool mark = idle && frame;
			
			if (redpak)
			{
				//Set mark
				redpak->SetMark(mark);

				//Send it
				rtp.SendPacket(*redpak);
			}
			
            //Calculate timeouts
            if ( redCodec.EncoderIsIdle() ) 
            {
                //By default wait for keep-alive
                timeout = 10000;
                idle = true;
            }
            else {
                
                //Normal timeout
                idle = false;
                timeout = 300;
            }

            delete redpak;
        }
    }


	//Salimos
	Log("<SendText\n");
	sendingText = TaskIdle;
	pthread_exit(0);
}

MediaStatistics TextStream::GetStatistics()
{
	MediaStatistics stats;

	//Fill stats
        rtp.GetStatistics(0, stats);
	stats.isReceiving	= IsReceiving();
	stats.isSending		= IsSending();

	//Return it
	return stats;
}

int TextStream::SetMute(bool isMuted)
{
	//Set it
	muted = isMuted;
	//Exit
	return 1;
}
