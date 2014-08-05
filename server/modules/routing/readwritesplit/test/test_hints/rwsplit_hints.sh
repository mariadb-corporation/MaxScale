#!/bin/bash
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
echo "        rwsplit.sh <log filename> <host> <port> <master id> <user> <password>"
echo ""
exit 1
fi

TESTINPUT=hints.txt
QUERY="select @@server_id;"
RUNCMD=mysql\ --host=$THOST\ -P$TPORT\ -u$TUSER\ -p$TPWD\ --unbuffered=true\ --disable-reconnect\ --silent\

while read -r LINE
do
TINPUT=`echo "$LINE"|awk '{split($0,a,":");print a[1]}'`
TRETVAL=`echo "$LINE"|awk '{split($0,a,":");print a[2]}'`
a=`$RUNCMD -e"$QUERY$TINPUT"`
if [ "$a" != "$TRETVAL" ]; then 
        echo "$TINPUT FAILED, return value $a when $TRETVAL was expected">>$TLOG; 
else 
        echo "$TINPUT PASSED">>$TLOG ; 
fi

done < $TESTINPUT
