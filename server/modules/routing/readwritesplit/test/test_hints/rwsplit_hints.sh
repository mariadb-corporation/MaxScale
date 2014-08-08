#!/bin/bash
NARGS=7
TLOG=$1
THOST=$2
TPORT=$3
TMASTER_ID=$4
TUSER=$5
TPWD=$6
TESTINPUT=$7

if [ $# != $NARGS ] ;
then
echo""
echo "Wrong number of arguments, gave "$#" but "$NARGS" is required"
echo "" 
echo "Usage :" 
echo "        rwsplit_hints.sh <log filename> <host> <port> <master id> <user> <password> <test file>"
echo ""
exit 1
fi


RUNCMD=mysql\ --host=$THOST\ -P$TPORT\ -u$TUSER\ -p$TPWD\ --unbuffered=true\ --disable-reconnect\ --silent\ --comment
i=0

while read -r LINE
do
TINPUT[$i]=`echo "$LINE"|awk '{split($0,a,":");print a[1]}'`
TRETVAL[$i]=`echo "$LINE"|awk '{split($0,a,":");print a[2]}'`
echo "${TINPUT[i]}" >> $TESTINPUT.sql
i=$((i+1))
done < $TESTINPUT

`$RUNCMD < $TESTINPUT.sql > $TESTINPUT.output`

x=0
crash=1
all_passed=1

while read -r TOUTPUT
do
crash=0
if [ "$TOUTPUT" != "${TRETVAL[x]}" -a "${TRETVAL[x]}" != "" ]
then 
    all_passed=0
    echo "$TESTINPUT:$((x + 1)): ${TINPUT[x]} FAILED, return value $TOUTPUT when ${TRETVAL[x]} was expected">>$TLOG; 
fi
x=$((x+1))
done < $TESTINPUT.output

if [ $crash -eq 1 ]
then
    all_passed=0
    for ((v=0;v<$i;v++))
    do
	echo "${TINPUT[v]} FAILED, nothing was returned">>$TLOG; 
    done
fi

if [ $all_passed -eq 1 ]
then
    echo "Test set: PASSED">>$TLOG; 
else
    echo "Test set: FAILED">>$TLOG; 
fi
