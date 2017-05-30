#! /bin/sh

srcdir=$1
THOST=$2
TPORT=$3
TUSER=$4
TPWD=$5
TMASTER_ID=$6
TESTFILE=$7

TEST=`basename $srcdir/$TESTFILE .test`

mkdir -p t r log || exit 3

if [ "$srcdir" != "." ]
then 
  cp $srcdir/t/$TEST.test   t || exit 3
  cp $srcdir/r/$TEST.result r || exit 3
  rm -f *.inc                 || exit 3
  cp $srcdir/*.inc          . || exit 3
fi

rm -f log/$TEST.log r/$TEST.reject r/$TEST.diff

echo "--disable_query_log" > testconf.inc
echo "SET @TMASTER_ID=$TMASTER_ID;" >> testconf.inc
echo "--enable_query_log" >> testconf.inc

RESULT=0

mysqltest --host=$THOST --port=$TPORT --user=$TUSER --password=$TPWD --logdir=log --test-file=$srcdir/t/$TEST.test --result-file=r/$TEST.result --silent $ssl_options || RESULT=1
if [ $? -ne 0 ] ; then
	exit 1
fi

if test -f r/$TEST.reject
then
  diff -u r/$TEST.result r/$TEST.reject > r/$TEST.diff
fi

if [ $RESULT -eq 0 ]
then
  rm -rf log testconf.inc
  if [ "$srcdir" != "." ]
  then
    rm -rf p t r log *.inc
  fi
fi


exit $RESULT
