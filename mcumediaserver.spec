Name:      %name
Version:   %version
#Ne pas enlever le .ives a la fin de la release !
#Cela est utilise par les scripts de recherche de package.
Release:   1.ives%{?dist}
Summary:   [IVeS] librairies partag꦳ pour asterisk de Fontventa.
Vendor:    IVeS / Fontventa
Group:     Applications/Internet
License:   GPL
URL:       http://www.ives.fr
BuildRoot:  %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
Requires:  x264, ImageMagick-c++ >= 6.7.0
BuildRequires: ffmpeg-devel, x264-devel, gcc-c++, bzip2-devel, ImageMagick-c++-devel >= 6.7.0, libbfcp >= 5.5.0

%ifos el5
Requires: fonts-chinese, fonts-japanese
%endif

%description
Mediaserver controlled by sailfin applications
%clean
echo "############################# Clean"
echo Clean du repertoire $RPM_BUILD_ROOT
[ "$RPM_BUILD_ROOT" != "/" ] && rm -rf "$RPM_BUILD_ROOT"
cd %_topdir
cd ../mcu
make -f Makefile.rpm clean
%prep
cd %_topdir
cd ..
./install.ksh webrtc

%build
echo "Build"
cd %_topdir
cd ..
./install.ksh localcompile

%install
echo "############################# Install"
mkdir -p $RPM_BUILD_ROOT/opt/ives/bin/
mkdir -p $RPM_BUILD_ROOT/etc/init.d
mkdir -p $RPM_BUILD_ROOT/etc/mediaserver
cd %_topdir
cd ..
cp bin/debug/mcu $RPM_BUILD_ROOT/opt/ives/bin/mediaserver
chmod 750 $RPM_BUILD_ROOT/opt/ives/bin/mediaserver
cp mediaserver.init $RPM_BUILD_ROOT/etc/init.d/mediaserver
cp type-asian.xml $RPM_BUILD_ROOT/etc/mediaserver

%files
%defattr(-,root,root,-)
/opt/ives/bin
%attr(0755,root,root) /etc/init.d/mediaserver
/etc/mediaserver/type-asian.xml

%post
if [ ! -r /etc/ImageMagick-6/type.xml ]
then
    echo "ImageMagick font are not configured. Participant name may not be displayed correctly"
else
    cp /etc/mediaserver/type-asian.xml /etc/ImageMagick-6/
    grep "type-asian.xml" > /dev/null
    if [ $? -ne 0 ]
    then
       echo "You need to change font configuration of ImageMagick for asian font support."
       echo 'Add the following line in type.xml: <include file="type-asian.xml" />'
    else
       echo "Asian font support has been correctly enabled"
    fi
fi
echo "Now restarting mediaserver"
/etc/init.d/mediaserver restart

%changelog
* Fri Sep 29 2017 Jeremy Huart <jeremy.huart@ives.fr>
- Fixed quantization flash by adding CFR_VBV encoding mode
* Tue Feb 03 2016 Thomas Carvello <thomas.carvello@ives.fr>
- Fixed ticket size mosaic
- Fixed deprecated function for ffmpeg
- Fixed packaging issue
- Fixed crash websocketconnection close
- Fixed lib dependency for compiling
- Fixed crash with mosaicPiP1
- Fixed crash issue with G722 codec (frame not initialized)
- Fixed Loop RTP
- version 1.6.16
* Fri Jan 15 2016 Thomas Carvello <thomas.carvello@ives.fr>
- Github compilation and packaging
- version 1.6.15
* Wed Dec 09 2015 Thomas Carvello <thomas.carvello@ives.fr>
- Adding --websocket-host for WSS
- version 1.6.14
* Mon Nov 23 2015 Thomas Carvello <thomas.carvello@ives.fr>
- Adding support for BFCP over UDP. New BFCP lib integration
- version 1.6.13
* Wed Oct 27 2015 Thomas Carvello <thomas.carvello@ives.fr>
- Merge from 1.5 (version 6935)
- Adding support dtls compilation for centos 5
- version 1.6.12
* Wed May 13 2015 Thomas Carvello <thomas.carvello@ives.fr>
- fixed RTP multiplexer regression 
- version 1.6.11
* Fri Mar 27 2015 Thomas Carvello <thomas.carvello@ives.fr>
- SIP INFO FIR support
- fixed ticket #32 #31 Omnitor
- version 1.6.10
* Fri Feb 06 2015 Thomas Carvello <thomas.carvello@ives.fr>
- integration code sergio DTLS + fix sequence numerotation issue(setrtpproperty useOriSeqNum)
- setmaxwaittime extend from 60 to 300
- fixed  crash packet issue.
- version 1.6.9
* Thu Dec 11 2014 Thomas Carvello <thomas.carvello@ives.fr>
- corrected redundancy text issue processing.
- fixed  issue with opus codec.
- version 1.6.8
* Fri Nov 07 2014 Thomas Carvello <thomas.carvello@ives.fr>
- corrected encoding text issue.
- fixed issue with payload type not synchronised with asterisk
- implemented auto transcoder.
- version 1.6.7
* Thu Sep 24 2014 Thomas Carvello <thomas.carvello@ives.fr>
- corrected redundancy text issue.
- version 1.6.6
* Thu Sep 11 2014 Emmanuel BUU <emmanuel.buu@ives.fr>
- corrected JSR309 endpoint to support webrtc to webrtc call
- tested regression on MCU function and corrected them.
- version 1.6.5
* Tue Sep 9 2014 Emmanuel BUU <emmanuel.buu@ives.fr>
- added audio transcoding in JSR 309 API
- added media statistics for JSR309 endpoint
- merged chages from 1.5 branch
- version 1.6.4
* Tue Sep 02 2014 Thomas CARVELLO <thomas.carvello@ives.fr>
- Fixed STUN negociation
- Fixed several encoding issue(G722,VP8)
- Audio transcoder implementation
- Fixed issue with redundancy T140
- version 1.6.2
* Fri Aug 01 2014 Thomas CARVELLO <thomas.carvello@ives.fr>
- added text over WebSocket support
- various JSR309 fixes for encoding/decoding problems. 
- added G.722.1 codec support
- if RTMP or WevSocket server port is already in use, mediaserver do not start
- version 1.6.1
* Fri Jul 11 2014 Emmanuel BUU <emmanuel.buu@ives.fr>
- added WebRTC support to MCU
- retrofited and tested WebRTC support in JSR309 API
- added codec parameters for JSR 309 VideoTranscoder
- version 1.6.0
* Tue Apr 08 2014 Thomas CARVELLO  <thomas.carvello@ives.fr>
- Corrected issue VAD VCUBE Ticket #173.
- version 1.5.16
* Fri Mar 21 2014 Thomas CARVELLO  <thomas.carvello@ives.fr>
- RTP Multiplexing.
- version 1.5.15
* Fri Feb 28 2014 Thomas CARVELLO  <thomas.carvello@ives.fr>
- Corrected issue VAD clean old slot.
- Corrected issue flash gateway audio RTMP -> RTP.
- Added AppMixer stop slot handling.
- version 1.5.14
* Thu Feb 18 2014 Thomas CARVELLO  <thomas.carvello@ives.fr>
- Corrected issue VAD clean old slot.
- version 1.5.13
* Thu Feb 14 2014 Thomas CARVELLO  <thomas.carvello@ives.fr>
- Fixed Htmltosip issue with severals received commands
- Fixed Crash in setVideoCodec 
- Added Compatibility for xmlrpc method
- Fixed setVideoSize issue with doc sharing
- Reimplemented logo with imagemagick
- Added Opus Support
- Activated WebSocket
- Added WebRTC corrections
- version 1.5.12
* Thu Feb 06 2014 Thomas CARVELLO  <thomas.carvello@ives.fr>
- Added secondary stream
- Added setdocsharingmosaic function
- version 1.5.11
* Mon Jan 27 2014 Thomas CARVELLO  <thomas.carvello@ives.fr>
- Added BFCP Library.
- Added BFCP Request/Server Handling.
- Fixed Volume level issue with Flash.
- version 1.5.10
* Fri Jan 17 2014 Thomas CARVELLO  <thomas.carvello@ives.fr>
- add parameters for recording
- corrected recording bug
- gestion evenement doc sharing
- correction reinvite
- version 1.5.9
* Fri Jan 03 2014 Thomas CARVELLO  <thomas.carvello@ives.fr>
- corrected a dead lock in stop text encoding.
- version 1.5.7
* Thu Dec 19 2013 Thomas CARVELLO  <thomas.carvello@ives.fr>
- First version of Document Sharing - No BFCP request handling.
- version 1.5.7
* Fri Dec 13 2013 Thomas CARVELLO  <thomas.carvello@ives.fr>
- corrected a crash case.
- version 1.5.6
* Wed Nov 13 2013 Thomas CARVELLO  <thomas.carvello@ives.fr>
- added  character code parameter for the display of the participant's name
- added update conference property (vadmode)
- added media server redundency
- version 1.5.4
* Mon Oct 28 2013 Emmanuel BUU <emmanuel.buu@ives.fr>
- added chinese / japananse character support
* Wed Oct 23 2013 Thomas CARVELLO  <thomas.carvello@ives.fr>
- Support of audio wide band
- Support of multiple mosaic displays
- Added display of the participant's name
- version 1.5.1
* Wed May 23 2013 Emmanuel BUU  <emmanuel.buu@ives.fr>
- corrected RTP session seq management causing "out of seq" traces
- tuning of remote rate control estimator
- version 1.4.4
* Wed May 15 2013 Emmanuel BUU  <emmanuel.buu@ives.fr>
- crash on rtpbuffer resync
- version 1.4.3
* Tue May 14 2013 Emmanuel BUU <emmanuel.buu@ives.fr>
- resync RTP buffer if too much packets out of sequence
- corrected RTCP nat traversal causing RTCP packet to be sent in RTP stream
- version 1.4.2
* Mon May 13 2013 Emmanuel BUU <emmanuel.buu@ives.fr>
- corrected a crash case.
- added logs regarding RTMP sockets.
- version 1.4.1
* Mon Apr 29 2013 Emmanuel BUU <emmanuel.buu@ives.fr>
- packaged version 1.4.0
* Fri Apr 12 2013 Emmanuel BUU <emmanuel.buu@ives.fr>
- corrected packetloss / FUR behavior
- audiomixer bug causing lock and loss of audio in some conditions.
- premliminary VP6 support (decoding only) for RTMP client
* Tue Apr 3 2013 Emmanuel BUU <emmanuel.buu@ives.fr>
- increased audiobuffer capacity (compatibility with iOS AIR SDK)
* Mon Mar 11 2013 Emmanuel BUU <emmanuel.buu@ives.fr>
- really corrected the socked fd 0 closure bug (by openning a dummy socket)
- correctred RTMP freeze (incosistent time stamps after 390 s)
- version 1.3.15
* Fri Mar 8 2013  Emmanuel BUU <emmanuel.buu@ives.fr>
- backport of RTMP corrections from 1.3 branch including, timestamp correction issues
- use of statically linked xmlrpc-c libraries
- additional correction fo correct closure of file descriptor 0.
* Mon Feb 25 2013 Emmanuel BUU <emmanuel.buu@ives.fr>
- corrected bug where RTMP server or XMLRPC server were shutdown when calling DeleteConference WebService
- version 1.3.14
* Wed Feb 13 2013 Emmanuel BUU <emmanuel.buu@ives.fr>
- compiled with xmlrpc c static
* Wed Feb 6 2013 Emmanuel BUU <emmanuel.buu@ives.fr>
- corrected frozen active speaker on secondarty mosaic
- corrected potential crash in video mixer
- removed annoying trace

* Wed Jan 25 2013 Emmanuel BUU <emmanuel.buu@ives.fr>
- corrected crash when removing a sidebar from a confernce
- corrected crash when calling SetCompositionType() twice, quickly on
  the same conference and mosaic.
- version 1.3.12
- more corrections on realtimetext
* Wed Jan 9 2013 Emmanuel BUU <emmanuel.buu@ives.fr>
- fixed non silent participant removal from VAD slot.
* Mon Dec 17 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- version 1.3.10
- RTT: fixed encoding and decoding of accented characters.
- improved reconnection mechanism for F2SIP
- many correction on advanced active speaker.
- added parameter to set the vad period change for mediaserver
- corrected PAL dimension causing mosaic 1+5 to have some overlapping
- corrected crash in text mixer
* Tue Dec 10 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- version 1.3.9
- advanced active speaker tested implementation
* Tue Dec 7 2012 Thomas CARVELLO <thomas.carvello@ives.fr>
- realtime text improvments: added support T.140 redundency in flash 2 sip
- version 1.3.8
* Thu Nov 29 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- less responsive to FUR
- archived webrtc in IVeS repo and changed build process accordingly
* Sun Oct 21 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- audio mixer correction
- fps conrol correction in rtmpparticipant
- version 1.3.7
* Mon Oct 8 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- version 1.3.6
- correction regression mosaic ID > 1
- correction of audiomixers when a participant listen to a sidebar without sending its audio into it.
* Wed Oct 3 2012  Emmanuel BUU <emmanuel.buu@ives.fr>
- version 1.3.5
- correction regression on SendPacket() - wrong test on media length
- removed division by zero crash
- correction in adding / removing participant automatically from default sidebar
* Mon Oct 1 2012 Philippe VERNEY <philippe.verney@ives.fr>
- version 1.3.4
- packaging improvments
- integration of text gateway related developments.
* Wed Sep 26 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- version 1.3.3
- support for ns.publish(false) used by FMS to close a stream
- H.264 packetization twicks
* Mon Sep 17 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- version 1.3.2
- fixed crash in H.264 decoding and RTMP participant
* Wed Sep 12 2012  Emmanuel BUU <emmanuel.buu@ives.fr>
- version 1.3.1
- correction on T.140
- further fix on RTMP robusness
- exposed some additional interfaces for sidebars
* Mon Sep 3 2012  Emmanuel BUU <emmanuel.buu@ives.fr>
- version 1.3.0 (internal release)
- first version with VAD support and sidebars (flexible audio mixing)
* Fri Aug 27 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- version 1.2.7
- previois valid decode patch caused H.264 stream not to be decoded
- corrected RTMP particpant on codec settings (bad init)
* Tue Aug 24 2012 Philippe VERNEY  <philippe.verney@ives.fr>
- version 1.2.6
- Check valid decode .
* Tue Aug 21 2012 Philippe VERNEY  <philippe.verney@ives.fr>
- version 1.2.5
- Skip bad H.264 packet on rtmp stream .
* Wed Aug 10 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- correction F2SIP wrong URLs
* Fri Aug 9 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- correction of on crash case on RTMP disconnection
- RTP restriction integration in new code
- version 1.2.3
- FMS disconnection (PingRequest) issue tested ok.
* Thu Aug 2 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- version 1.2.2 
- fixed PingRequest issue
- idendifed and fixed locking issue
* Thu Aug 1 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- version 1.2.0 
- RTMP connection reuse
* Fri Jul 13 2012 Philippe VERNEY  <philippe.verney@ives.fr>
- version 1.1.0
- Add FMStranscoder module .
* Thu Jun 21 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- version 1.0.1
- another attempt to correct rtmp participant crash during disconenct
- hard coded logo in /etc/mediaserver/logo.png instead of CWD.
* Thu May 31 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- version 1.0.0
- reconnect event to triger conf reconnect
* Wed May 30 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- version 0.9.7
- fix another RTMP participan crash in StopSending()
* Tue May 29 2012 Olivier MAQUIN <olivier.maquin@ives.fr>
- version 0.9.6
- compilation and packaging for Centos6
* Mon May 14 2012 Philippe VERNEY  <philippe.verney@ives.fr>
- version 0.9.5
- fixed bad fix on RTMP participant crash ...
* Thu May 3 2012 Emmanuel BUU <philippe.verney@ives.fr>
- version  0.9.4
- restricted RTP port to 15000 2000 range.
* Fri Apr 27 2012 Emmanuel BUU <philippe.verney@ives.fr>
- version 0.9.3
- corrected run condition in for Flash participant that caused mcu to crash.
* Wed Apr 18 2012 Philippe VERNEY <philippe.verney@ives.fr>
- version 0.9.2
- fixed lag of RTPsmoother
- new H.264 encoding settings
* Tue Apr 17 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- version 0.9.1
- fixed RTMP ack
- fixed video mute / audio mute
- added more I-Frame sending at the beginning of RTP session (pverney) as a patch
- fixed negative conference IDs
* Mon Apr 16 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- version 0.9.0
- PAL / 4CIF and HVGA definition are now supported.
- PNG overlay and PIP in mosaic.
* Fri Apr 6 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- version 0.8.7
- work on x264 encoder bandwidth management.
* Wed Mar 29 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- packagingg: changed binary to be in /opt/ives/bin/mediaserver
- version 0.8.6
- handle SSRC changes
- reintroduced latest NAT stuff from Sergio
* Mon Mar 27 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- version 0.8.5
- previous fix was not working. Reimplementing mre lax fix for NAT.
* Mon Mar 26 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- version 0.8.4
- fixed NAT handling.
* Sat Mar 24 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- version 0.8.3
- regression fixed on hangup deadlock (missing patch was not included).
* Thu Mar 22 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- version 0.8.2
- added mutex in multiconf class to prevent crash in case of concurrent DeleteParticipant command
- removed public mcuWeb from the archive
- integrated last rtmpparticipant fixes related to reconnection of RTMP sockets (fixed integration mistake)
- send kill twice in /etc/init.d/mediaserver script to really kill the process when stopping
- removed uneeded dependency to mpeg4ip (mp4v2 is used and linked statically)
* Sat Mar 17 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- version 0.8.1
- added recorder in JS309
- fixed RTMP reconnect.
- added event queue to call controller but no implementation of event
* Tue Mar 13 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- version 0.8.0
- reintregration des modifs de Sergio
- comprend integration du NAT et evenement MCU vers controlleur
- on enleve nat.patch
* Mon Feb 27 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- fixed one Deadlock and a crash case in DeleteParticipant for rtmp particopants.
- version 0.7.x
* Wed Feb 15 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- fix for picture freeze on flash when the last SIP participant leaves the conf
* Wed Feb 1 2012  Emmanuel BUU <emmanuel.buu@ives.fr>
- H.264 publish support for RTMP participant
- RTMP participant crash fix
* Wed Jan 25 2012 Emmanuel BUU <emmanuel.buu@ives.fr>
- participant par flash
- support nellymoser 11 KhZ
* Wed Jan 18 2012  Emmanuel BUU <emmanuel.buu@ives.fr>
- fpsaccounting + traces erreur RTP
* Wed Jan 11 2012  Sergio Garcia <sergio.garcia@fontventa.com>
- reintegration dernier sources de Sergio afec flv1
* Fri Dec 9 2011  Emmanuel BUU <emmanuel.buu@ives.fr>
- hardcoded intra period of 10 sec (flash2sip)
* Thu Dec 1 2011 Emmanuel BUU <emmanuel.buu@ives.fr>
- Initial package
