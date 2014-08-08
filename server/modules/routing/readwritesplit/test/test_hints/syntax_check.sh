#! /bin/bash
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
echo "        syntax_check.sh <log filename> <host> <port> <master id> <user> <password> <test file>"
 echo ""
exit 1
fi

./rwsplit_hints.sh dummy.log $THOST $TPORT $TMASTER_ID $TUSER $TPWD $TESTINPUT

exp_count=`cat error_tests | grep -c '.*'`
err_count=`tail -n $exp_count ../../../../../test/log/skygw_err*|grep -c 'Hint ignored'`

if [ "$err_count" == "$exp_count" ]
then
    echo "Test set: PASSED">>$TLOG; 
else
    echo "Expected $exp_count ignored hints in the error log but found $err_count instead">>$TLOG
    echo "Test set: FAILED">>$TLOG; 
fi
