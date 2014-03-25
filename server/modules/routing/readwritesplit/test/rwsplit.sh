#!/bin/sh
TLOG=$1
THOST=$2
TPORT=$3
TUSER=$4
TPWD=$5
RUNCMD=mysql\ --host=$THOST\ -P$TPORT\ -u$TUSER\ -p$TPWD\ --unbuffered=true\ --disable-reconnect\ --silent

INPUT=test_transaction_routing1.sql

a=`$RUNCMD < ./$INPUT`
if [ "$a" != "2" ]; then echo "$INPUT FAILED">>$TLOG; exit 1 ; 
else echo "$INPUT PASSED">>$TLOG ; fi

