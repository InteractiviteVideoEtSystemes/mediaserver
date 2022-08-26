#!/bin/bash

PROJET=mcumediaserver
VERSION="1.8.6"
#Repertoire d'installation des includes
DESTDIR_INC=/usr/include/
#Repertoire d'installation des librairies
if [ "`uname -m`" == "x86_64" ]
then
	DESTDIR_LIB=/usr/lib64
else
	DESTDIR_LIB=/usr/lib
fi
WEBRTCTAG=0.0.1
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
		svn export https://svn.ives.fr/svn-libs-dev/gnupg
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
    rm -rf $HOME/xmlrpc-c
    rm -f staticdeps/lib/libxmlrpc*.a
	
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
  	# On efface les liens ainsi que le package precedemment cr.
  	echo Effacement des fichiers et liens gnupg rpmbuild ${PROJET}.rpm ${TEMPDIR}/${PROJET}
  	rm -rf gnupg rpmbuild ${TEMPDIR}/${PROJET}
	cd mcu 
	make -f Makefile.rpm clean
	cd -
}

function compile_webrtc_from_google
{
	MEDIASERVERPATH=$PWD
	echo "compiling some Webrtc libs used in the mcu"
	cd $HOME
	if [ -x /urr/local/bin/python ]
	then
		hash python /usr/local/bin/python
		echo "using specific python installlation"
	fi

	python -V 2> version
	if [ $? -ne 0 ]
	then
		echo "python doit etre installe en version 2.6 ou superieure"
		return
	fi
	
	echo "now checking installed version"
	egrep "( 2.6| 2.7)" version
	if [ $? -ne 0 ]
	then
		echo "version incorrecte de python :"
		python -V
		rm -r version
		echo " il faut installer une version 2.6 ou 2.7 "
		return
	fi

	rm -f version	
	if [ ! -r depot_tools ]
	then
		echo getting chromium depot tools
		svn co http://src.chromium.org/chrome/trunk/tools/depot_tools
	fi

	export PATH=$PATH:$HOME/depot_tools
	if [ ! -r  webrtc/trunk/webrtc/common_audio ]
	then
		echo downloading webrtc
		mkdir webrtc
		cd webrtc
		gclient config http://webrtc.googlecode.com/svn/trunk
		gclient sync --force
		gclient runhooks --force
		echo building webrtc
		cd trunk
		#sed -e 's/BUILDTYPE ?= Debug/BUILDTYPE ?= Release/g' Makefile > Makefike.new
		mv Makefile Makefile.sav
		#mv Makefike.new Makefile
		cp $MEDIASERVERPATH/media/Makefile.webrtc Makefile
	else
		cd webrtc/trunk
		cp $MEDIASERVERPATH/media/Makefile.webrtc Makefile
	fi

	if [ ! -r out/Release/obj.target/webrtc/common_audio/libsignal_processing.a ]
	then
		make out/Release/obj.target/webrtc/common_audio/libsignal_processing.a
		make out/Release/obj.target/webrtc/common_audio/libvad.a
		
	fi

}

function compile_webrtc_ives
{
	MEDIASERVERPATH=$PWD
	cd $HOME
        if [ ! -r  webrtc/trunk/webrtc/common_audio ]
        then
                echo downloading webrtc
                mkdir webrtc
                cd webrtc
				svn co http://svn.ives.fr/svn-libs-dev/webrtc/tags/$WEBRTCTAG trunk
                cd trunk
        else
                cd webrtc/trunk
        fi

        if [ ! -r out/Release/obj.target/webrtc/common_audio/libsignal_processing.a ]
        then
                make out/Release/obj.target/webrtc/common_audio/libsignal_processing.a
                make out/Release/obj.target/webrtc/common_audio/libvad.a
	fi

	cd $MEDIASERVERPATH
}

function compile_webrtc
{
	MEDIASERVERPATH=$PWD
	cd $HOME
        if [ ! -r  webrtc/trunk/webrtc/common_audio ]
        then
                echo downloading webrtc
                git clone https://github.com/neutrino38/webrtc_stack.git
		mv webrtc_stack webrtc
                cd webrtc/trunk
        else
                cd webrtc/trunk
        fi

        if [ ! -r out/Release/obj.target/webrtc/common_audio/libsignal_processing.a ]
        then
		echo "Compiling VAD and signal processing from webrtc"
                make out/Release/obj.target/webrtc/common_audio/libsignal_processing.a
		git reset --hard
                make out/Release/obj.target/webrtc/common_audio/libsignal_processing.a
                make out/Release/obj.target/webrtc/common_audio/libvad.a
	fi

	cd $MEDIASERVERPATH
}

function archive_webrtc
{
	MEDIASERVERPATH=$PWD
	if [ "`uname -m`" == "x86_64" ]
	then
		if [ ! -r lib/linux-64bits/libvad.a ]
		then
			echo "archiving 64 bit lib"
			mkdir -p lib/linux-64bits
			cp $HOME/webrtc/trunk/out/Release/obj.target/webrtc/common_audio/libvad.a lib/linux-64bits
			cp $HOME/webrtc/trunk/out/Release/obj.target/webrtc/common_audio/libsignal_processing.a lib/linux-64bits
		else
			echo "libvad was not compiled !!!"
		fi
	else
		if [ ! -r lib/linux-64bits/libvad.a ]
		then
			echo "archiving 32 bits lib"
			mkdir -p lib/linux-32bits
			cp $HOME/webrtc/trunk/out/Release/obj.target/webrtc/common_audio/libvad.a lib/linux-32bits
			cp $HOME/webrtc/trunk/out/Release/obj.target/webrtc/common_audio/libsignal_processing.a lib/linux-32bits
		else
			echo "libvad was not compiled !!!"
		fi
		
	fi
	echo "archving headers"
	# TODO archive headers
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

	# compiler openssl en statique
	BASESRCDIR=$PWD
	if [ ! -f staticdeps/lib/libssl.a ]
	then
		echo "on doit compiler openssl 1.1.1d"
		if [ ! -r $HOME/openssl ]
		then
			cd $HOME
			rm -f OpenSSL_1_1_1d.tar.gz
			wget https://github.com/openssl/openssl/archive/OpenSSL_1_1_1d.tar.gz

			tar xzf OpenSSL_1_1_1d.tar.gz
			mv openssl-OpenSSL_1_1_1d openssl
			rm -f OpenSSL_1_1_1d.tar.gz
		fi
		cd $HOME/openssl
		./config --prefix=$BASESRCDIR/staticdeps --openssldir=$BASESRCDIR/staticdeps shared zlib
		make
		make install
		cd $BASESRCDIR
	fi

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

	
	# compiler speex en statique
	BASESRCDIR=$PWD
	if [ ! -f staticdeps/lib/libspeex.a ]
	then
		echo "on doit compiler SPEEX 1.2rc1"
		if [ ! -r $HOME/speex-src ]
		then
			#svn export http://svn.ives.fr/svn-libs-dev/asterisk/libsmedia/speex/tags/1.2rc1 $HOME/speex-src
			cd $HOME
			rm -f speex-1.2rc2.tar.gz speex-1.2rc2.tar
			wget http://downloads.xiph.org/releases/speex/speex-1.2rc2.tar.gz
			tar xzf speex-1.2rc2.tar.gz
			mv speex-1.2rc2 speex-src
			cd $HOME/speex-src
		fi
		cd $HOME/speex-src
			./configure --prefix=$BASESRCDIR/staticdeps --exec-prefix=$BASESRCDIR/staticdeps --enable-shared=no
		make
		make install
		cd $BASESRCDIR
	fi

	if [ ! -f staticdeps/lib/libspeexdsp.a ]
	then
		echo "on doit compiler SPEEXDSP 1.2rc1"
		if [ ! -r $HOME/speexdsp-src ]
		then
			#svn export http://svn.ives.fr/svn-libs-dev/asterisk/libsmedia/speex/tags/1.2rc1 $HOME/speex-src
			cd $HOME
			rm -f speexdsp-1.2rc3.tar.gz
			wget http://downloads.xiph.org/releases/speex/speexdsp-1.2rc3.tar.gz
			tar xzf speexdsp-1.2rc3.tar.gz
			mv speexdsp-1.2rc3 speexdsp-src
			cd $HOME/speexdsp-src
		fi
		cd $HOME/speexdsp-src
			./configure --prefix=$BASESRCDIR/staticdeps --exec-prefix=$BASESRCDIR/staticdeps --enable-shared=no
		make
		make install
		cd $BASESRCDIR
	fi


	# http://xmlrpc-c.svn.sourceforge.net/viewvc/xmlrpc-c/super_stable?view=tar
	if [ ! -f staticdeps/lib/libxmlrpc_abyss.a ]
	then
		echo "compilation XMLRPC-C"
		cd $HOME
		if [ ! -f xmlrpc-c ]
		then
			#svn export svn://svn.code.sf.net/p/xmlrpc-c/code/release_number/01.39.06 xmlrpc-c
			svn export svn://svn.code.sf.net/p/xmlrpc-c/code/stable xmlrpc-c
		fi
		cd xmlrpc-c
		./configure --disable-abyss-openssl --prefix=$BASESRCDIR/staticdeps --exec-prefix=$BASESRCDIR/staticdeps --enable-shared=no
		make
		make install
		cd $BASESRCDIR
	fi


	if [ ! -f staticdeps/lib/libsrtp.a ]
	then
		echo "compilation SRTP"
		cd $HOME
		if [ ! -f srtp ]
		then
			 git clone https://github.com/InteractiviteVideoEtSystemes/patchedLibSRTP.git srtp
		fi
		cd srtp
		chmod 755 configure
		./configure --prefix=$BASESRCDIR/staticdeps --exec-prefix=$BASESRCDIR/staticdeps --enable-shared=no
		make
		make uninstall
		make install
		cd $BASESRCDIR
	fi
		
	if [ ! -f staticdeps/lib/libopus.a ]
	then
		echo "compilation OPUS"
		cd $HOME
		
		if [ ! -f opus-1.1 ]
		then
			wget http://downloads.xiph.org/releases/opus/opus-1.1.tar.gz
			tar xzf opus-1.1.tar.gz
			rm -f  opus-1.1.tar.gz  opus-1.1.tar
		fi
		cd opus-1.1
		./configure --prefix=$BASESRCDIR/staticdeps --exec-prefix=$BASESRCDIR/staticdeps --enable-shared=no
		make
		make install
		cd $BASESRCDIR
	fi
		

	if [ ! -f staticdeps/lib/libg722_1.a ]
	then
		echo "compilation SIREN / G.722.1"
		cd $HOME
		if [ ! -r libg722_1 ]
		then
			#svn export http://svn.ives.fr/svn-libs-dev/libg722_1
			git clone https://github.com/neutrino38/libg722_1.git
		fi
		cd libg722_1
		./configure --prefix=$BASESRCDIR/staticdeps --exec-prefix=$BASESRCDIR/staticdeps --enable-shared=no
		make clean
		make
		make install
		cd $BASESRCDIR
	fi
	
	if [ ! -r staticdeps/lib/libvpx.a ]
	then
    		echo "Compiling VP8 codec"
		#cd $HOME/webrtc/trunk/third_party/libvpx/source/libvpx
		cd $HOME
		rm -rf libvpx
		git clone https://chromium.googlesource.com/webm/libvpx
		cd libvpx
		git checkout v1.7.0
		chmod 750 configure
		./configure --disable-shared --enable-static --prefix=$BASESRCDIR/staticdeps  --enable-vp8 --enable-error-concealment  --disable-multithread 
		make
		make install
		cd $BASESRCDIR
	fi	
	compile_webrtc;
	cd $BASESRCDIR
	
	mkdir -p bin/debug
	cd mcu
	make -f Makefile.rpm mcu
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

	"webrtc")
		compile_webrtc;;
	
	"webrtcives")	
		compile_webrtc_ives;;

        "rabbitmq")
		compile_rabbitmq;;
		
	"protobuf")
		compile_protobuf;;

	"upload")
		upload_rpm ;;
	"prereq")
		sudo yum install -y gsm-devel ;;
  	*)
  		echo "usage: install.ksh [options]" 
  		echo "options :"
  		echo "  rpm		Generation d'un package rpm"
		echo "  localcompile    Compilation du logiciel sans creation de paquet rpm"
		echo "  webrtc          Compilation des libs webrtc"
		echo "  rabbitmq        Compilation des libs RABBITMQ (projet moteli)"
		echo "  upload          TODO: envoi les paquets RPM dans le repo"
  		echo "  clean		Nettoie tous les fichiers cree ce script, liens, tar.gz et rpm";;

esac

