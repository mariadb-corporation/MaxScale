#! /bin/sh
if [[ $# -ne 4 ]]
then
    echo "Usage: canontest.sh <logfile name> <input file> <output file> <expected output>"
    exit 0
fi
TESTLOG=$1
INPUT=$2
OUTPUT=$3
EXPECTED=$4
DIFFLOG=diff.out
$PWD/canonizer $INPUT $OUTPUT
diff $OUTPUT $EXPECTED > $DIFFLOG
if [ $? -eq 0 ]
then 
    echo "PASSED"		>> $TESTLOG
else
    echo "FAILED"		>> $TESTLOG 
    echo "Diff output: "	>> $TESTLOG
    cat $DIFFLOG		>> $TESTLOG
fi
