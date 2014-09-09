#! /bin/bash

if [[ $# -lt 3 ]]
then
    echo "Usage: logorder.sh <iterations> <frequency of flushes> <message size>"
    echo "To disable log flushing, use 0 for flush frequency"
    exit
fi

rm *.log

#Create large messages
$PWD/testorder $1 $2 $3

MCOUNT=$1

STARTS=`cat skygw_err1.log |tr -s ' '|grep -o 'start:[[:digit:]]\+'|cut -d ':' -f 2`
MESSAGES=`cat skygw_err1.log |tr -s ' '|grep -o 'message|[[:digit:]]\+'|cut -d '|' -f 2`
ENDS=`cat skygw_err1.log |tr -s ' '|grep -o 'end:[[:digit:]]\+'|cut -d ':' -f 2`

prev=0
error=0

for i in $STARTS
do

    if [[ $i -le $prev ]]
    then
	error=1
	echo "start mismatch: $i was after $prev."
    fi
    prev=$i
done

if [[ error -eq 0 ]]
then
    echo "Block buffer starts were in ascending order"
else
    echo "Error: block buffers were written in the wrong order"
fi

prev=0
error=0

for i in $MESSAGES
do

    if [[ $i -ne $(( prev + 1 )) ]]
    then
	error=1
	echo "message mismatch: $i was after $prev."
    fi
    prev=$i
done

if [[ error -eq 0 ]]
then
    echo "Block buffer messages were in ascending order"
else
    echo "Error: block buffer messages were written in the wrong order"
fi

prev=0
error=0

for i in $ENDS
do
    if [[ $i -le $prev ]]
    then
	error=1
	echo "end mismatch: $i was after $prev."
    fi
    prev=$i
done

if [[ error -eq 0 ]]
then
    echo "Block buffer ends were in ascending order"
else
    echo "Error: block buffers were written in the wrong order"
fi
