#! /bin/bash

if [[ $# -lt 4 ]]
then
    echo "Usage: logorder.sh <iterations> <frequency of flushes> <message size> <log file>"
    echo "To disable log flushing, use 0 for flush frequency"
    exit
fi

if [ $# -eq 5 ]
then
    TDIR=$5
else
    TDIR=$PWD
fi
rm $TDIR/*.log

#Create large messages

$TDIR/testorder $1 $2 $3

TESTLOG=$4
MCOUNT=$1

BLOCKS=`cat $TDIR/skygw_err1.log |tr -s ' '|grep -o 'block:[[:digit:]]\+'|cut -d ':' -f 2`
MESSAGES=`cat $TDIR/skygw_err1.log |tr -s ' '|grep -o 'message|[[:digit:]]\+'|cut -d '|' -f 2`

prev=0
error=0
all_errors=0

for i in $BLOCKS
do

    if [[ $i -le $prev ]]
    then
	error=1
	all_errors=1
	echo "block mismatch: $i was after $prev."  >> $TESTLOG
    fi
    prev=$i
done

if [[ error -eq 0 ]]
then
    echo "Block buffers were in order"  >> $TESTLOG
else
    echo "Error: block buffers were written in the wrong order"  >> $TESTLOG
fi

prev=0
error=0

for i in $MESSAGES
do

    if [[ $i -ne $(( prev + 1 )) ]]
    then
	error=1
	all_errors=1
	echo "message mismatch: $i was after $prev."  >> $TESTLOG
    fi
    prev=$i
done

if [[ error -eq 0 ]]
then
    echo "Block buffer messages were in order" >> $TESTLOG
else
    echo "Error: block buffer messages were written in the wrong order"  >> $TESTLOG
fi

cat $TESTLOG
exit $all_errors
