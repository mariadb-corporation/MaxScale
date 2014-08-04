#!/bin/sh                                                                                                                                                              
NARGS=6
TLOG=$1
THOST=$2
TPORT=$3
TMASTER_ID=$4
TUSER=$5
TPWD=$6

if [ $# != $NARGS ] ;
then
echo""
echo "Wrong number of arguments, gave "$#" but "$NARGS" is required"
echo ""
echo "Usage :"
echo "        rwsplit_hints.sh <log filename> <host> <port> <master id> <user> <password>"
echo ""
exit 1
fi
QUERY=select\ @@server_id
RUNCMD=mysql\ --host=$THOST\ -P$TPORT\ -u$TUSER\ -p$TPWD\ --unbuffered=true\ --disable-reconnect\ --silent\ -e"$QUERY $HINT"

HINT=--\ maxscale\ route\ to\ master
a=`$RUNCMD`
echo "$a"

