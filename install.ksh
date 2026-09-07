#!/bin/bash

PROJET=mcumediaserver
VERSION="1.14.0"
#Repertoire d'installation des includes
DESTDIR_INC=/usr/include/
#Repertoire d'installation des librairies
if [ "`uname -m`" == "x86_64" ]
then
	DESTDIR_LIB=/usr/lib64
else
	DESTDIR_LIB=/usr/lib
fi

#RPepertoire d'installation des fichiers so
DESTDIR_MOD=$DESTDIR_LIB/asterisk/modules
#Repertoire d'installation du fichier mp4tool
DESTDIR_BIN=/usr/bin/
#Repertoire temporaire utiliser pour preparer les packages
TEMPDIR=/tmp

#Creation de l'environnement de packaging rpm
function create_rpm
{
    #Cree l'environnement de creation de package
    #Creation des macros rpmbuild
    rm ~/.rpmmacros
    touch ~/.rpmmacros
	
    echo "%name" $PROJET >> ~/.rpmmacros
    echo "%version" $VERSION >> ~/.rpmmacros
    echo "%_topdir" $PWD"/rpmbuild" >> ~/.rpmmacros
    echo "%_tmppath %{_topdir}/TMP" >> ~/.rpmmacros
    echo "%_signature gpg" >> ~/.rpmmacros
    echo "%_gpg_name IVeSkey" >> ~/.rpmmacros
    echo "%_gpg_path" $PWD"/gnupg" >> ~/.rpmmacros
    echo "%vendor IVeS" >> ~/.rpmmacros
    if [[ -z $2 || $2 -ne nosign ]]
	then
		#Import de la clef gpg IVeS
		#svn export https://svn.ives.fr/svn-libs-dev/gnupg
		rm -rf gnupg
		git clone git@git.ives.fr:internal/gnupg.git
    fi
    mkdir -p rpmbuild
    mkdir -p rpmbuild/SOURCES
    mkdir -p rpmbuild/SPECS
    mkdir -p rpmbuild/BUILD
    mkdir -p rpmbuild/SRPMS
    mkdir -p rpmbuild/TMP
    mkdir -p rpmbuild/RPMS
    mkdir -p rpmbuild/RPMS/noarch
    mkdir -p rpmbuild/RPMS/i386
    mkdir -p rpmbuild/RPMS/i686
    mkdir -p rpmbuild/RPMS/i586
    mkdir -p rpmbuild/RPMS/x86_64
    #Recuperation de la description du package 
    cd ./rpmbuild/SPECS/
    cp ../../mcumediaserver.spec .
    cd ../../
    if [[ -z $2 || $2 -ne nosign ]]
	then
		rpmbuild -bb --sign $PWD/rpmbuild/SPECS/mcumediaserver.spec
	else
		rpmbuild -bb $PWD/rpmbuild/SPECS/mcumediaserver.spec
	fi
	if [ $? == 0 ]
	then
		echo "************************* fin du rpmbuild ****************************"
		#Recuperation du rpm
		mv -f $PWD/rpmbuild/RPMS/i386/*.rpm $PWD/.
		mv -f $PWD/rpmbuild/RPMS/i586/*.rpm $PWD/.
		mv -f $PWD/rpmbuild/RPMS/i686/*.rpm $PWD/.
		mv -f $PWD/rpmbuild/RPMS/x86_64/*.rpm $PWD/.
	clean
	else
	clean
	echo "*** error during build ***"
	exit 20
	fi
}

function clean
{
	BASESRCDIR=$PWD
	MEDKITDIR=$BASESRCDIR/third_party/fontventa/libmedikit
	BFCPDIR=$BASESRCDIR/third_party/libbfcp

  	# On efface les liens ainsi que le package precedemment cr.
  	echo Effacement des fichiers et liens gnupg rpmbuild ${PROJET}.rpm ${TEMPDIR}/${PROJET}
  	rm -rf gnupg rpmbuild ${TEMPDIR}/${PROJET}

	# Nettoyage du binaire et des objets du mediaserver.
	cd mcu
	make clean
	cd "$BASESRCDIR"

	# Nettoyage des objets et archives des sous-modules (libmedkit + libbfcp),
	# pour qu'un "clean" reparte reellement d'un arbre vierge. On garde les memes
	# options que la construction (compile_libmedkit / compile_libbfcp).
	if [ -f "$MEDKITDIR/Makefile" ]
	then
		echo "Nettoyage libmedkit (in-tree) : objets + libmedkit.a"
		make -C "$MEDKITDIR" clean ASTERISK=no
		# Balayage defensif : la cible clean ne retire que les .o de la liste OBJS
		# courante ; on supprime aussi les .o residuels d'anciennes listes/builds
		# (aucun .o n'est suivi par git dans le sous-module).
		find "$MEDKITDIR" -name '*.o' -delete
		rm -f "$MEDKITDIR/libmedkit.a"
	fi
	if [ -f "$BFCPDIR/Makefile" ]
	then
		echo "Nettoyage libbfcp (in-tree) : objets + libbfcp{dbg,rel}.a"
		make -C "$BFCPDIR" clean DEBUG=yes
		make -C "$BFCPDIR" clean DEBUG=no
		find "$BFCPDIR" -name '*.o' -delete
		rm -f "$BFCPDIR"/lib/libbfcp*.a "$BFCPDIR"/lib/libbfcp*.so
	fi
}

function local_compile
{
	# compiler localement
	echo checking if dependencies are installed
	rpm -q gsm-devel
    	if [ $? != 0 ]
	then
		echo "installer gsm-devel"
		exit 20
	fi

	rpm -q ffmpeg-devel
    if [ $? != 0 ]
	then
		echo "installer ffmpeg-free-devel depuis RPMFUSION free et non free"
		exit 20
	fi

	rpm -q libtool
    if [ $? != 0 ]
	then
		echo "installer libtool"
		exit 20
	fi

	rpm -q webrtc-audio-processing-devel
    if [ $? != 0 ]
	then
		echo "installer webrtc-audio-processing-devel"
		exit 20
	fi

	# libsrtp2 (l'ABI utilisee) est fournie par le paquet libsrtp-devel, pas libsrtp2-devel.
	rpm -q libsrtp-devel
    if [ $? != 0 ]
	then
		echo "installer libsrtp-devel"
		exit 20
	fi

	# xmlrpc-c : plus construit depuis les sources, on utilise le paquet systeme.
	rpm -q xmlrpc-c-devel
    if [ $? != 0 ]
	then
		echo "installer xmlrpc-c-devel (depot crb)"
		exit 20
	fi


	# compiler openssl en statique
	BASESRCDIR=$PWD

	# compiler mp4v2 en static 
	if [ ! -f staticdeps/lib/libmp4v2.a ]
	then
		echo "compilation libmp4v2"
		cd $HOME
		if [ ! -r mp4v2 ]
		then
			git clone https://github.com/InteractiviteVideoEtSystemes/mp4v2.git
		fi
		cd mp4v2
		./configure --prefix=$BASESRCDIR/staticdeps --exec-prefix=$BASESRCDIR/staticdeps --enable-shared=no
		make clean
		make
		make install
		cd $BASESRCDIR
	fi
	
	# speex : plus de build statique. Le codec Speex est fourni par libmedikit
	# au-dessus de ffmpeg (AV_CODEC_ID_SPEEX, cf. libmedikit/speex/speexcodec.cpp)
	# et la ligne de lien el9 par defaut ne reference plus -lspeex.

	# libspeexdsp : plus utilisee. Le reechantillonnage audio (ex-AudioTransrater)
	# passe desormais par libswresample (ffmpeg), deja lie via -lswresample.

	# xmlrpc-c : plus construit depuis les sources. On s'appuie desormais sur le
	# paquet systeme xmlrpc-c-devel (AlmaLinux 9, depot crb) : memes en-tetes,
	# meme backend libxml2, lie dynamiquement (voir mcu/Makefile LDXMLFLAGS).

	# g722_1 / SIREN : plus construit. Le codec G.722.1 a ete retire du
	# mediaserver et de libmedikit (plus aucune reference a -lg722_1).

	cd $BASESRCDIR

	# Sous-modules (libmedkit = codecs, libbfcp = BFCP) : on les initialise au
	# besoin puis on construit leurs archives in-tree, pour qu'un seul
	# "install.ksh localcompile" suffise a produire le binaire.
	if [ ! -f third_party/fontventa/libmedikit/medkit/media.h ] || [ ! -f third_party/libbfcp/Makefile ]
	then
		echo "initialisation des sous-modules (libmedikit, libbfcp)"
		git submodule update --init --recursive
	fi
	compile_libmedkit
	compile_libbfcp

	cd $BASESRCDIR

	mkdir -p bin/debug
	cd mcu
	make mcu
}

function compile_rabbitmq
{
	MEDIASERVERPATH=$PWD
	rpm -q cmake > /dev/null
	if [ $? != 0 ]
	then
		echo "Installer cmake (sudo yum install cmake)"
		exit 20
	fi

	if [ ! -r staticdeps/lib/librabbitmq.a ]
	then
		echo "Compilation RABBITMQ-C"
		cd $HOME
		svn export http://svn.ives.fr/svn-libs-dev/rabbitmq-c/tags/0.3.0 rabbitmq-c
		cd rabbitmq-c
		mkdir build
		cd build
		cmake -DCMAKE_INSTALL_PREFIX=$MEDIASERVERPATH/staticdeps -DBUILD_STATIC_LIBS=1 -DBUILD_SHARED_LIBS=0 ..
		make
		make install
	fi

	if [ ! -r staticdeps/lib/libamqpcpp.a ]
	then
		echo "Compilation AMPQCPP"
		cd $HOME
		svn export http://svn.ives.fr/svn-libs-dev/ampqcpp/trunk ampqcpp
		cd ampqcpp
		make clean
		make INSTALLPREFIX=$MEDIASERVERPATH/staticdeps lib
		cp libamqpcpp.a $MEDIASERVERPATH/staticdeps/lib
		cp include/AMQPcpp.h $MEDIASERVERPATH/staticdeps/include
		
	fi
	cd $MEDIASERVERPATH
}

function compile_libmedkit
{
	# Construit libmedkit.a DANS l'arbre du sous-module (cible 'all', pas
	# d'install dans /opt/ives). Le mediaserver s'y lie directement via
	# MEDKITDIR/USEMEDKIT dans mcu/Makefile. Voir almalinux9_port_plan.md.
	MEDIASERVERPATH=$PWD
	MEDKITDIR=$MEDIASERVERPATH/third_party/fontventa/libmedikit
	if [ ! -d "$MEDKITDIR" ]
	then
		echo "Sous-module libmedikit absent. Lancer : git submodule update --init"
		exit 20
	fi
	echo "Compilation libmedkit (in-tree)"
	# Plus de surcharge d'INCLUDE ici : les chemins d'en-tetes sont TOUS dans le
	# Makefile du sous-module. ffmpeg y vient de pkg-config, staticdeps/include
	# du defaut relatif (equivalent au chemin absolu qu'on injectait, make
	# tournant en -C), et FFMPEGINC/MP4V2INC y restent utilisables comme
	# variables d'environnement.
	#
	# Cet ecrasement etait actif ET nuisible : il masquait un `-I` sans argument
	# du Makefile (CUSTOM_ASTPATH vide en build non-Asterisk), qui faisait avaler
	# a gcc l'option suivante comme repertoire d'include. Un `make` direct dans le
	# sous-module compilait donc SANS -DLOG_, contrairement au build officiel :
	# deux verites pour un meme arbre, et la mauvaise etait la plus accessible.
	#
	# ASTERISK=no : on exclut les objets couples a Asterisk (transcoder, mp4format,
	# framebuffer, frameutils, astlog), inutilisables hors module Asterisk et qui
	# exigeraient asterisk-devel. Le mediaserver n'est pas un module Asterisk.
	# On laisse le Makefile choisir la liste OBJS (source unique de verite : elle
	# inclut mp4reader.o/mp4writer.o dont depend le mediaserver via mp4streamer/
	# mp4recorder). Ne plus surcharger OBJS ici pour eviter la desynchronisation.
	make -C "$MEDKITDIR" all ASTERISK=no
	cd $MEDIASERVERPATH
}

function compile_libbfcp
{
	# Construit libbfcp DANS l'arbre du sous-module (cible 'all', pas d'install
	# dans /opt/ives). Le mediaserver s'y lie directement via BFCPDIR dans
	# mcu/Makefile. On produit les deux variantes (dbg + rel) pour couvrir
	# les deux valeurs de DEBUG du build mcu.
	MEDIASERVERPATH=$PWD
	BFCPDIR=$MEDIASERVERPATH/third_party/libbfcp
	if [ ! -f "$BFCPDIR/Makefile" ]
	then
		echo "Sous-module libbfcp absent. Lancer : git submodule update --init"
		exit 20
	fi
	if [ ! -f "$BFCPDIR/lib/libbfcpdbg.a" ]
	then
		echo "Compilation libbfcp (in-tree, debug)"
		make -C "$BFCPDIR" all DEBUG=yes
	fi
	if [ ! -f "$BFCPDIR/lib/libbfcprel.a" ]
	then
		echo "Compilation libbfcp (in-tree, release)"
		make -C "$BFCPDIR" all DEBUG=no
	fi
	cd $MEDIASERVERPATH
}

function compile_protobuf
{
	MEDIASERVERPATH=$PWD

	if [ ! -r staticdeps/lib/libprotobuf.a ]
	then
		echo "Compilation PROTOBUF"
		cd $HOME
		svn export http://svn.ives.fr/svn-libs-dev/protobuf/tags/2.5.0 protobuf
		cd protobuf
		./configure --prefix=$MEDIASERVERPATH/staticdeps --exec-prefix=$MEDIASERVERPATH/staticdeps --enable-shared=no
		make
		make install
		cd $BASESRCDIR
	fi
	cd $MEDIASERVERPATH
}


case $1 in
  	"clean")
  		echo "Nettoyage des liens et du package crees par la cible dev"
  		clean ;;
  	"rpm")
		echo "Creation du rpm"
		create_rpm "$@";;
	"export")
        echo "{" >> build.properties
        echo "'VERSION': '$VERSION'," >> build.properties
        echo "'PROJET':'$PROJET'," >> build.properties
        echo "'DESTDIR':'$DESTDIR'" >> build.properties
        echo "}" >> build.properties
       ;;
	"localcompile")
		local_compile;;

    "rabbitmq")
		compile_rabbitmq;;

	"protobuf")
		compile_protobuf;;

	"libmedkit")
		compile_libmedkit;;

	"libbfcp")
		compile_libbfcp;;

	"upload")
		upload_rpm ;;
	"prereq")
		sudo yum install -y gsm-devel ffmpeg-devel webrtc-audio-processing-devel libsrtp-devel xmlrpc-c-devel usrsctp-devel ;;
  	*)
  		echo "usage: install.ksh [options]" 
  		echo "options :"
  		echo "  rpm				Generation d'un package rpm"
		echo "  localcompile	Compilation du logiciel sans creation de paquet rpm"
		echo "  rabbitmq        Compilation des libs RABBITMQ (projet moteli)"
		echo "  libmedkit       Compilation de libmedkit.a (sous-module, in-tree)"
		echo "  libbfcp         Compilation de libbfcp (sous-module, in-tree)"
		echo "  upload          TODO: envoi les paquets RPM dans le repo"
  		echo "  clean			Nettoie les fichiers crees par ce script (liens, rpm) + les objets/archives de mcu et des sous-modules (libmedkit, libbfcp)";;
esac
