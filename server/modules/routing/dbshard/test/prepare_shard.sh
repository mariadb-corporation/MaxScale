#!/bin/bash
if [ $# -lt 3 ]
then
    echo "usage $0 <host> <port> <username> <password>"
    exit 1
fi
HOST=$1
PORT=$2
USER=$3
PW=$4
SHD="shard$RANDOM"
mysql -u $USER -p$PW -P $PORT -h $HOST -e "create database $SHD;"
echo "Created database \"$SHD\" at $HOST:$PORT"
