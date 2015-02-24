#!/bin/bash
if [ $# -lt 5 ]
then
    printf "Not enough arguments: '"
    for i in $@
    do
        printf "$i "
    done
    echo "'given, 5 needed."
    echo "usage $0 <host> <port> <username> <password> <database name>"
    exit 1
fi
HOST=$1
PORT=$2
USER=$3
PW=$4
SHD=$5
mysql -u $USER -p$PW -P $PORT -h $HOST -e "create database $SHD;"
echo "Created database \"$SHD\" at $HOST:$PORT"
