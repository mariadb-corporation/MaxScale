#!/bin/sh
failure=0
passed=0
maxadmin -pmariadb help >& /dev/null
if [ $? -eq "1" ]; then
	echo "Auth test (correct password):		Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Auth test (correct password):		Passed"
fi
maxadmin -pwrongpasswd help >& /dev/null
if [ $? -eq "0" ]; then
	echo "Auth test (wrong password):		Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Auth test (wrong password):		Passed"
fi
maxadmin --password=mariadb help >& /dev/null
if [ $? -eq "1" ]; then
	echo "Auth test (long option):		Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Auth test (long option):		Passed"
fi

#
# Test enable|disable heartbeat|root without, with invalid and with long invalid argument
#
for op in enable disable
do
for cmd in heartbeat root
do
	maxadmin -pmariadb $op $cmd >& /dev/null
	if [ $? -eq "1" ]; then
	        echo "$op $cmd (missing arg):        	Failed"
	        failure=`expr $failure + 1`
	else
	        passed=`expr $passed + 1`
	        echo "$op $cmd (missing arg):		Passed"
	fi

	maxadmin -pmariadb $op $cmd qwerty >& /dev/null
	if [ $? -eq "1" ]; then
	        echo "$op $cmd (invalid arg):		Failed"
	        failure=`expr $failure + 1`
	else
	        passed=`expr $passed + 1`
	        echo "$op $cmd (invalied arg): 		Passed"
	fi

	maxadmin -pmariadb $op $cmd xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx >& /dev/null
	if [ $? -eq "1" ]; then
	        echo "$op $cmd (long invalid arg):	Failed"
	        failure=`expr $failure + 1`
	else
	        passed=`expr $passed + 1`
	        echo "$op $cmd (long invalid arg):	Passed"
	fi
done
done

#
# Test reload dbusers with short, and long garbage and without argument
#
maxadmin -pmariadb reload dbusers qwerty >& /dev/null
if [ $? -eq "1" ]; then
        echo "Reload dbusers (invalid arg):		Failed"
        failure=`expr $failure + 1`
else
        passed=`expr $passed + 1`
        echo "Reload dbusers (invalid arg):             Passed"
fi

maxadmin -pmariadb reload dbusers xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx >& /dev/null
if [ $? -eq "1" ]; then
        echo "Reload dbusers (long invalid arg):        Failed"
        failure=`expr $failure + 1`
else
        passed=`expr $passed + 1`
        echo "Reload dbusers (long invalid arg):	Passed"
fi


maxadmin -pmariadb reload dbusers >& /dev/null
if [ $? -eq "1" ]; then
        echo "Reload dbusers (missing arg):             Failed"
        failure=`expr $failure + 1`
else
        passed=`expr $passed + 1`
        echo "Reload dbusers (missing arg):         	Passed"
fi      

#
# Test enable|disable log debug|trace|message|error
#

for action in enable disable
do
    maxadmin -pmariadb $action log debug >& /dev/null
    if [ $? -eq "1" ]; then
	    echo "$action debug log:			Failed"
	    failure=`expr $failure + 1`
    else
	    passed=`expr $passed + 1`
	    echo "$action debug log:			Passed"
    fi

    maxadmin -pmariadb $action log trace >& /dev/null
    if [ $? -eq "1" ]; then
	    echo "$action trace log:			Failed"
	    failure=`expr $failure + 1`
    else
	    passed=`expr $passed + 1`
	    echo "$action trace log:			Passed"
    fi

    maxadmin -pmariadb $action log message >& /dev/null
    if [ $? -eq "1" ]; then
        echo "$action message log:                 Failed"
        failure=`expr $failure + 1`
    else
        passed=`expr $passed + 1`
        echo "$action message log:                 Passed"
    fi

    maxadmin -pmariadb $action log error >& /dev/null
    if [ $? -eq "1" ]; then
        echo "$action error log:                 Failed"
        failure=`expr $failure + 1`
    else
        passed=`expr $passed + 1`
        echo "$action error log:                 Passed"
    fi
done

#
# Test restart monitor|service without, with invalid and with long invalid argument
#
for cmd in monitor service
do
	maxadmin -pmariadb restart $cmd >& /dev/null
	if [ $? -eq "1" ]; then
        	echo "Restart $cmd (missing arg):      	Failed"
       		failure=`expr $failure + 1`
	else
        	passed=`expr $passed + 1`
	        echo "Restart $cmd (missing arg):	Passed"
	fi

	maxadmin -pmariadb restart $cmd xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx >& /dev/null
	if [ $? -eq "1" ]; then
        	echo "Restart $cmd (long invalid arg):	Failed"
        	failure=`expr $failure + 1`
		else
        	passed=`expr $passed + 1`
        	echo "Restart $cmd (long invalid arg):	Passed"
	fi

	maxadmin -pmariadb restart $cmd qwerty >& /dev/null
	if [ $? -eq "1" ]; then
        	echo "Restart $cmd (invalid arg): 	Failed"
        	failure=`expr $failure + 1`
	else
        	passed=`expr $passed + 1`
        	echo "Restart $cmd (invalid arg):	Passed"
	fi
done

#
# Test set server qwerty master without, with invalid and with long invalid arg
#
maxadmin -pmariadb set server qwerty >& /dev/null
if [ $? -eq "1" ]; then
        echo "Set server qwerty (missing arg):		Failed"
        failure=`expr $failure + 1`
else
        passed=`expr $passed + 1`
        echo "Set server (missing arg):                 Passed"
fi

maxadmin -pmariadb set server qwerty mamaster >& /dev/null
if [ $? -eq "1" ]; then
        echo "Set server qwerty (invalid arg):		Failed"
        failure=`expr $failure + 1`
else
        passed=`expr $passed + 1`
        echo "Set server qwerty (invalid arg):		Passed"
fi

maxadmin -pmariadb set server qwerty xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx >& /dev/null
if [ $? -eq "1" ]; then
        echo "Set server qwerty (long invalid arg):	Failed"
        failure=`expr $failure + 1`
else
        passed=`expr $passed + 1`
        echo "Set server qwerty (long invalid arg):	Passed"
fi


for cmd in clients dcbs filters listeners modules monitors services servers sessions threads
do
	maxadmin -pmariadb list $cmd | grep -s '-' >& /dev/null
	if [ $? -eq "1" ]; then
		echo "list command ($cmd):  		Failed"
		failure=`expr $failure + 1`
	else
		passed=`expr $passed + 1`
		echo "list command ($cmd):  		Passed"
	fi
done

for cmd in dcbs dbusers epoll filters modules monitors services servers sessions threads users
do
	maxadmin -pmariadb show $cmd | grep -s ' ' >& /dev/null
	if [ $? -eq "1" ]; then
		echo "show command ($cmd):			Failed"
		failure=`expr $failure + 1`
	else
		passed=`expr $passed + 1`
		echo "show command ($cmd):			Passed"
	fi
done

master=`maxadmin -pmariadb list servers | awk  '/Master/ { print $1; }'`
if [ $? -eq "1" ]; then
	echo "Extract master server:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Extract master server:			Passed"
fi
if [ "$master" = "" ]; then
	echo "Get master server:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Get master server:			Passed"
fi
maxadmin -pmariadb show server $master | grep -s 'Master' >& /dev/null
if [ $? -eq "1" ]; then
	echo "show server master:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "show server master:			Passed"
fi

maxadmin -pmariadb set server $master maint >& /dev/null
if [ $? -eq "1" ]; then
	echo "set server $master maint:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "set server $master maint:			Passed"
fi

maxadmin -pmariadb list servers | grep $master | grep -s 'Maint' >& /dev/null
if [ $? -eq "1" ]; then
	echo "set maintenance mode:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "set maintenance mode:			Passed"
fi

maxadmin -pmariadb clear server $master maint >& /dev/null
if [ $? -eq "1" ]; then
	echo "clear server:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "clear server:				Passed"
fi
maxadmin -pmariadb list servers | grep $master | grep -s 'Maint' >& /dev/null
if [ $? -eq "0" ]; then
	echo "clear maintenance mode:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "clear maintenance mode:			Passed"
fi

dcbs=`maxadmin -pmariadb list dcbs | awk -F\| '/listening/ { if ( NF > 1 )  print $1 }'`
if [ $? -eq "1" ]; then
	echo "Get dcb listeners:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Get dcb listeners:			Passed"
fi

for i in $dcbs
do
	maxadmin -pmariadb show dcb $i | grep -s 'listening' >& /dev/null
	if [ $? -eq "1" ]; then
		echo "show dcb listeners:		Failed"
		failure=`expr $failure + 1`
	else
		passed=`expr $passed + 1`
		echo "show dcb listeners:		Passed"
	fi
done

#
# Test show dcb|eventq|eventstats|filter|monitor|server|service|session with invalid arg
#
for cmd in dcb eventq filter monitor server service sessions
do
        maxadmin -pmariadb show $cmd qwerty | grep -s '-' >& /dev/null
        if [ $? -eq "0" ]; then
                echo "show $cmd (invalid arg):		Failed"
                failure=`expr $failure + 1`
        else
                passed=`expr $passed + 1`
                echo "show $cmd (invalid arg):		Passed"
        fi
done

#
# Test shutdown monitor|service with invalid extra argument
#
for cmd in monitor service 
do
        maxadmin -pmariadb shutdown $cmd qwerty | grep -s '-' >& /dev/null
        if [ $? -eq "0" ]; then
                echo "Shutdown $cmd (invalid extra arg):Failed"
                failure=`expr $failure + 1`
        else
                passed=`expr $passed + 1`
                echo "Shutdown $cmd (invalid extra arg):Passed"
        fi
done


sessions=`maxadmin -pmariadb list sessions | awk -F\| '/Listener/ { if ( NF > 1 )  print $1 }'`
if [ $? -eq "1" ]; then
	echo "Get listener sessions:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Get listener sessions:			Passed"
fi

for i in $sessions
do
	maxadmin -pmariadb show session $i | grep -s 'Listener' >& /dev/null
	if [ $? -eq "1" ]; then
		echo "show session listeners:			Failed"
		failure=`expr $failure + 1`
	else
		passed=`expr $passed + 1`
		echo "show session listeners:			Passed"
	fi
done

filters=`maxadmin -pmariadb list filters | awk -F\| '{ if ( NF > 1 )  print $1 }'| grep -v Options`
if [ $? -eq "1" ]; then
	echo "Get Filter list:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Get filter list:			Passed"
fi

for i in $filters
do
	maxadmin -pmariadb show filter $i | grep -s 'Filter' >& /dev/null
	if [ $? -eq "1" ]; then
		echo "show filter:				Failed"
		failure=`expr $failure + 1`
	else
		passed=`expr $passed + 1`
		echo "show filter:				Passed"
	fi
done

maxadmin -pmariadb list services | \
	awk -F\| '{ if (NF > 1) { sub(/ +$/, "", $1); printf("show service \"%s\"\n", $1); } }' > script1.$$
grep -cs "show service" script1.$$ >/dev/null
if [ $? -ne "0" ]; then
	echo "list services:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "list services:				Passed"
fi
maxadmin -pmariadb script1.$$ | grep -cs 'Service' > /dev/null
if [ $? -ne "0" ]; then
	echo "Show Service:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Show Service:				Passed"
fi
rm -f script1.$$


maxadmin -pmariadb list monitors | \
	awk -F\| '{ if (NF > 1) { sub(/ +$/, "", $1); printf("show monitor \"%s\"\n", $1); } }' > script1.$$
grep -cs "show monitor" script1.$$ >/dev/null
if [ $? -ne "0" ]; then
	echo "list monitors:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "list monitors:				Passed"
fi
maxadmin -pmariadb script1.$$ | grep -cs 'Monitor' > /dev/null
if [ $? -ne "0" ]; then
	echo "Show Monitor:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Show Monitor:				Passed"
fi
rm -f script1.$$


maxadmin -pmariadb list sessions | \
	awk -F\| ' /^0x/ { if (NF > 1) { sub(/ +$/, "", $1); printf("show session \"%s\"\n", $1); } }' > script1.$$
grep -cs "show session" script1.$$ >/dev/null
if [ $? -ne "0" ]; then
	echo "list sessions:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "list sessions:				Passed"
fi
maxadmin -pmariadb script1.$$ | grep -cs 'Session' > /dev/null
if [ $? -ne "0" ]; then
	echo "Show Session:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Show Session:				Passed"
fi
rm -f script1.$$


maxadmin -pmariadb list dcbs | \
	awk -F\| ' /^ 0x/ { if (NF > 1) { sub(/ +$/, "", $1); sub(/ 0x/, "0x", $1); printf("show dcb \"%s\"\n", $1); } }' > script1.$$
grep -cs "show dcb" script1.$$ >/dev/null
if [ $? -ne "0" ]; then
	echo "list dcbs:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "list dcbs:				Passed"
fi
maxadmin -pmariadb script1.$$ | grep -cs 'DCB' > /dev/null
if [ $? -ne "0" ]; then
	echo "Show DCB:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Show DCB:				Passed"
fi
rm -f script1.$$


maxadmin -pmariadb list services | \
	awk -F\| '{ if (NF > 1) { sub(/ +$/, "", $1); printf("show dbusers \"%s\"\n", $1); } }' > script1.$$
grep -cs "show dbusers" script1.$$ >/dev/null
if [ $? -ne "0" ]; then
	echo "list services:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "list services:				Passed"
fi
maxadmin -pmariadb script1.$$ | grep -cs 'Users table data' > /dev/null
if [ $? -ne "0" ]; then
	echo "Show dbusers:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Show dbusers:				Passed"
fi
rm -f script1.$$


echo "Test run complete. $passed passes, $failure failures"
exit $failure
