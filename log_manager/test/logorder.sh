#! /bin/bash

#create the logfile with flushes every seven writes

rm *.log

$PWD/testorder 200 0 8000

STARTS=`cat skygw_err1.log |tr -s ' '|grep -a -o 'start:[[:alnum:]]\+'|cut -d ':' -f 2`
MESSAGES=`cat skygw_err1.log |tr -s ' '|grep -a -o 'message:[[:alnum:]]\+'|cut -d ':' -f 2`
ENDS=`cat skygw_err1.log |tr -s ' '|grep -a -o 'end:[[:alnum:]]\+'|cut -d ':' -f 2`

prev=0
error=0

for i in $STARTS
do
    if [[ $i -le $prev ]]
    then
	error=1
	echo "start mismatch: $i was after $prev."
    fi
done

if [[ error -eq 0 ]]
then
    echo "Block buffer starts were in order"
else
    echo "Error: block buffers were written in the wrong order"
fi

prev=0
error=0

for i in $MESSAGES
do
    if [[ $i -le $prev ]]
    then
	error=1
	echo "message mismatch: $i was after $prev."
    fi
done

if [[ error -eq 0 ]]
then
    echo "Block buffer messages were in order"
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
done

if [[ error -eq 0 ]]
then
    echo "Block buffer ends were in order"
else
    echo "Error: block buffers were written in the wrong order"
fi
