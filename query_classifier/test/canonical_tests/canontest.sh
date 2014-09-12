#! /bin/sh
if [[ $# -lt 4 ]]
then
    echo "Usage: canontest.sh <path to executable> <input file> <output file> <expected output>"
    exit 0
fi
EXECUTABLE=$1
INPUT=$2
OUTPUT=$3
EXPECTED=$4
DIFFLOG=diff.out
$EXECUTABLE $INPUT $OUTPUT
diff $OUTPUT $EXPECTED > $DIFFLOG
if [ $? -eq 0 ]
then 
    echo "PASSED"
else
    echo "FAILED"
    echo "Diff output: "
    cat $DIFFLOG
    exit 1;
fi
exit 0;
