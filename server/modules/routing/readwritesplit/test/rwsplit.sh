#!/bin/sh
NARGS=6
TLOG=$1
TINPUT=$2
THOST=$3
TPORT=$4
TUSER=$5
TPWD=$6

if [ $# != $NARGS ] ;
then
echo""
echo "Wrong number of arguments, gave "$#" but "$NARGS" is required"
echo "" 
echo "Usage :" 
echo "        rwsplit.sh <log filename> <test input> <host> <port> <user> <password>"
echo ""
exit 1
fi


RUNCMD=mysql\ --host=$THOST\ -P$TPORT\ -u$TUSER\ -p$TPWD\ --unbuffered=true\ --disable-reconnect\ --silent

a=`$RUNCMD < ./$TINPUT`
if [ "$a" != "2" ]; then 
        echo "$TINPUT FAILED">>$TLOG; 
else 
        echo "$TINPUT PASSED">>$TLOG ; 
fi

