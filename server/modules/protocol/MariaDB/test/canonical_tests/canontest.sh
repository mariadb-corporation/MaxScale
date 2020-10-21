#! /bin/sh
if [ $# -lt 4 ]
then
    echo "Usage: canontest.sh <logfile name> <input file> <output file> <expected output>"
    exit 0
fi
TESTLOG=$1
INPUT=$2
OUTPUT=$3
EXPECTED=$4
DIFFLOG=diff.out

if [ $# -eq 5 ]
then
    EXECUTABLE=$5
else
    EXECUTABLE=$PWD/canonizer
fi

$EXECUTABLE $INPUT $OUTPUT
diff $OUTPUT $EXPECTED > $DIFFLOG
if [ $? -eq 0 ]
then
    echo "PASSED"		>> $TESTLOG
    exval=0
else
    echo "FAILED"		>> $TESTLOG
    echo "Diff output: "	>> $TESTLOG
    cat $DIFFLOG		>> $TESTLOG
    exval=1
fi

if [ $# -eq 5 ]
then
    cat $TESTLOG
    exit $exval
fi
