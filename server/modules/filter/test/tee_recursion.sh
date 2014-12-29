#!/bin/bash

function execute_test()
{
    mysql -u $USER -p$PWD -h $HOST -P $PORT -e"select 1;"

    if [[ $? -eq 0 ]]
    then
        echo "Test failed: query to backend was successful."
        return 1
    fi

    LAST_LOG=$(ls $BINDIR/log -1|grep err|sort|uniq|tail -n 1)
    TEST_RESULT=$(cat $BINDIR/log/$LAST_LOG | grep -i recursive)
    if [[ "$TEST_RESULT" != "" ]]
    then

        return 0
    fi
    
    return 1
}

function reload_conf()
{
    $BINDIR/bin/maxadmin --user=admin --password=skysql reload config
    if [[ $? -ne 0 ]]
    then
        echo "Test failed: maxadmin returned a non-zero value."
        return 1
    fi
    return 0
}

if [[ $# -lt 6 ]]
then
    echo "usage: $0 <build dir> <source dir>"
    exit 1
fi
BINDIR=$1
SRCDIR=$2
USER=$3
PWD=$4
HOST=$5
PORT=$6
CONF=$BINDIR/etc/MaxScale.cnf
OLDCONF=$BINDIR/etc/MaxScale.cnf.old
TEST1=$SRCDIR/server/modules/filter/test/tee_recursion1.cnf
TEST2=$SRCDIR/server/modules/filter/test/tee_recursion2.cnf

$BINDIR/bin/maxadmin --user=admin --password=skysql flush logs
execute_test
mv $CONF $OLDCONF

cp $TEST1 $CONF
reload_conf
execute_test
T1RVAL=$?
mv $CONF $CONF.test1
cp $TEST2 $CONF
reload_conf
execute_test
T2RVAL=$?
mv $CONF $CONF.test2
mv $OLDCONF $CONF
reload_conf

if [[ $T1RVAL -ne 0 ]]
then
    echo "Test 1 failed."
    exit 1
elif [[ $T2RVAL -ne 0 ]]
then
    echo "Test 2 failed"
    exit 1
else
    echo "Test successful: log mentions recursive tee usage."
fi

exit 0
