# readme.md pour accompagner la generation

## Compilation

Un script est present à la racine du projet 'install.ksh'.

Plusieurs arguments sont possibles, pour une compialtion:

```./install.ksh localcompile```

Pour une simple recompilation:

```make -f mcu/Makefile.rpm mcu```

Le binaire resultat de cette compialtion est dans 'bin/debug/' avec comme nom de fichier 'mcu'

## Execution 

Pour préparer une utilisation, se deplacer dans le répertoire du binaire (dans le contexte IVèS):

```cd /opt/ives/bin/```

Faire une sauvegarde du binaire courant:

```mv mediaserver mediaserver.release```

Créer un lien symbolique vers ce nouveau binaire fraichement compiler

```ln -s /home/user/mediaserver/bin/debug/mcu mediaserver```

Redemarrer l'application:

```/etc/init.d/mediaserver restart```

Pour suivre l'évolution de l'execution:

```tail -f /var/log/mcu.log```
