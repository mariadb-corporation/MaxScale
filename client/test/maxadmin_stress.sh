#!/bin/sh
failure=0
passed=0

clients=20
cmdcnt=1000

echo Running $clients parallel iterations of $cmdcnt commands

for ((cnt=0; cnt<$clients; cnt++ )); do
	for ((i=0; i<$cmdcnt; i++ )); do
		maxadmin -pmariadb show services;
	done >/dev/null &
done >& /dev/null

peak=0
while [ "`jobs -p`" != "" ]; do
	jobs >& /dev/null
	zombies=`maxadmin -pmariadb list dcbs | grep -ci zombies`
	if [ $zombies -gt $peak ] ; then
		peak=$zombies
	fi
	sleep 1
done
if [ $peak -gt 10 ]; then
	echo "High peak zombie count ($peak):			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Zombie collection ($peak):			Passed"
fi
zombies=`maxadmin -pmariadb list dcbs | grep -ci zombies`
if [ $zombies != "0" ]; then
	echo "Residual zombie DCBs:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Residual zombie DCBs:			Passed"
fi
sessions=`maxadmin -pmariadb list services | awk -F\| '/ cli/ { print $3 }'`
if [ $sessions -gt 3 ]; then
	echo "Session shutdown, $sessions:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Session shutdown:			Passed"
fi

sessions=`maxadmin -pmariadb list services | awk -F\| '/ cli/ { print $4 }'`

echo "Test run complete. $passed passes, $failure failures"
echo "$sessions CLI sessions executed"
exit $failure
