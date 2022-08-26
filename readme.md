Pour compiler

./install.ksh

./install.ksh localcompile

recompilation uniquement
make -f mcu/Makefile.rpm mcu

resultat dans 'bin/debug/' executable 'mcu'

se deplacer dans le répertoire du binaire dans le contexte IVèS
cd /opt/ives/bin/

faire une sauvegarde du binaire courant
mv mediaserver mediaserver.release

créer un lien symbolique vers ce nouveau binaire fraichement compiler
ln -s /home/user/mediaserver/bin/debug/mcu mediaserver

redemarrer l'application
/etc/init.d/mediaserver restart

suivi de l'execution
tail -f /var/log/mcu.log


