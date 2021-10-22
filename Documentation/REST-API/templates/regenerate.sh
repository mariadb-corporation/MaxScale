#!/bin/bash

if [ $# -ne 1 ]
then
    echo "USAGE: <path to MaxScale sources>"
    exit 1
fi

SRCDIR="$1"
USER="maxuser"
PASSWORD="maxpwd"
SCRIPTDIR=$PWD

mkdir -p /tmp/build \
    && cd /tmp/build \
    && cmake -DWITH_SCRIPTS=N -DWITH_MAXSCALE_CNF=N -DCMAKE_INSTALL_PREFIX=/tmp/build/ $SRCDIR \
    && make -j 8 install \
    && mkdir -p {cache,run}/maxscale \
    && (cd $SRCDIR/test/ && docker-compose up -d) \
    && echo "docker ok" \
    && bin/maxscale -f rest_api.cnf


mysql -u $USER -p$PASSWORD -h 127.0.0.1 -P 4006 -e "select 1;select sleep(30)" &
sleep 1

(cd $SCRIPTDIR && ./generate.py)

pkill maxscale
wait
