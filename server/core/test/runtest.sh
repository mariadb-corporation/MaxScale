#!/bin/bash
test=$1
log=$2
echo Running test $test					>> $log
./$test						       2>> $log
if [ $? -ne 0 ]; then
	echo $test "		" FAILED		>> $log
else
	echo $test "		" PASSED		>> $log
fi
