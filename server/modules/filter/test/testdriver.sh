#! /bin/bash
if [[ $# -lt 4 ]]
then
    echo "Usage: $0 <config file> <input> <output> <expected>"
    exit 1
fi

TESTDIR=@CMAKE_CURRENT_BINARY_DIR@
SRCDIR=@CMAKE_CURRENT_SOURCE_DIR@
$TESTDIR/harness -i $SRCDIR/$2 -o $TESTDIR/$3 -c $TESTDIR/$1 -t 1 -s 1 -e $SRCDIR/$4 
exit $?
