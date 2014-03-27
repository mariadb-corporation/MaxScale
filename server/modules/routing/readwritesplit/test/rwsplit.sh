#!/bin/sh
NARGS=5
TLOG=$1
THOST=$2
TPORT=$3
TUSER=$4
TPWD=$5

if [ $# != $NARGS ] ;
then
echo""
echo "Wrong number of arguments, gave "$#" but "$NARGS" is required"
echo "" 
echo "Usage :" 
echo "        rwsplit.sh <log filename> <host> <port> <user> <password>"
echo ""
exit 1
fi


RUNCMD=mysql\ --host=$THOST\ -P$TPORT\ -u$TUSER\ -p$TPWD\ --unbuffered=true\ --disable-reconnect\ --silent

TINPUT=test_transaction_routing2.sql
TRETVAL=0
a=`$RUNCMD < ./$TINPUT`
if [ "$a" != "$TRETVAL" ]; then 
        echo "$TINPUT FAILED, return value $a when $TRETVAL was expected">>$TLOG; 
else 
        echo "$TINPUT PASSED">>$TLOG ; 
fi

TINPUT=test_transaction_routing3.sql
TRETVAL=2
a=`$RUNCMD < ./$TINPUT`
if [ "$a" != "$TRETVAL" ]; then 
        echo "$TINPUT FAILED, return value $a when $TRETVAL was expected">>$TLOG; 
else 
        echo "$TINPUT PASSED">>$TLOG ; 
fi
