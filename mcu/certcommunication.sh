#!/bin/bash
#cd /etc/mediaserver

if [ ! -r mcu.key ]
then
    # use mcu0 as pass key
    openssl genrsa -out mcu2.key 1024 
    openssl rsa -in mcu2.key -out mcu.key
    rm mcu2.key
    rm -f mcu.crt 
else
    echo "Key is aleady created. Delete mcu.key to recreate"
fi

if [ ! -r mcu.crt ]
then
    rm -f mcu.csr
    cp mcu.csr_conf mcu.csr_conf.tmp
    echo "commonName = " `hostname` >> mcu.csr_conf.tmp
    echo "commonName_default = " `hostname` >> mcu.csr_conf.tmp
    openssl req -new -key mcu.key -out mcu.csr -config mcu.csr_conf.tmp -batch
    openssl x509 -req -days 365 -in mcu.csr -signkey mcu.key -out mcu.crt
    rm -f mcu.csr_conf.tmp
else
    echo "Certificate is already created. Delete mcu.crt to recreate"
fi
  

