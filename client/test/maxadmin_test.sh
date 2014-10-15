#!/bin/sh
failure=0
passed=0
maxadmin -pskysql help >& /dev/null
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
maxadmin --password=skysql help >& /dev/null
if [ $? -eq "1" ]; then
	echo "Auth test (long option):		Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Auth test (long option):		Passed"
fi

maxadmin -pskysql enable log debug >& /dev/null
if [ $? -eq "1" ]; then
	echo "Enable debug log:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Enable debug log:			Passed"
fi

maxadmin -pskysql enable log trace >& /dev/null
if [ $? -eq "1" ]; then
	echo "Enable trace log:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Enable trace log:			Passed"
fi

maxadmin -pskysql disable log debug >& /dev/null
if [ $? -eq "1" ]; then
	echo "Disable debug log:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Disable debug log:			Passed"
fi

maxadmin -pskysql disable log trace >& /dev/null
if [ $? -eq "1" ]; then
	echo "Disable trace log:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Disable trace log:			Passed"
fi

for cmd in clients dcbs filters listeners modules monitors services servers sessions threads
do
	maxadmin -pskysql list $cmd | grep -s '-' >& /dev/null
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
	maxadmin -pskysql show $cmd | grep -s ' ' >& /dev/null
	if [ $? -eq "1" ]; then
		echo "show command ($cmd):			Failed"
		failure=`expr $failure + 1`
	else
		passed=`expr $passed + 1`
		echo "show command ($cmd):			Passed"
	fi
done

master=`maxadmin -pskysql list servers | awk  '/Master/ { print $1; }'`
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
maxadmin -pskysql show server $master | grep -s 'Master' >& /dev/null
if [ $? -eq "1" ]; then
	echo "show server master:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "show server master:			Passed"
fi

maxadmin -pskysql set server $master maint >& /dev/null
if [ $? -eq "1" ]; then
	echo "set server:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "set server:				Passed"
fi
maxadmin -pskysql list servers | grep $master | grep -s Maint >& /dev/null
if [ $? -eq "1" ]; then
	echo "set maintenance mode:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "set maintenance mode:			Passed"
fi
maxadmin -pskysql clear server $master maint >& /dev/null
if [ $? -eq "1" ]; then
	echo "clear server:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "clear server:				Passed"
fi
maxadmin -pskysql list servers | grep $master | grep -s Maint >& /dev/null
if [ $? -eq "0" ]; then
	echo "clear maintenance mode:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "clear maintenance mode:			Passed"
fi

dcbs=`maxadmin -pskysql list dcbs | awk -F\| '/listening/ { if ( NF > 1 )  print $1 }'`
if [ $? -eq "1" ]; then
	echo "Get dcb listeners:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Get dcb listeners:			Passed"
fi

for i in $dcbs
do
	maxadmin -pskysql show dcb $i | grep -s 'listening' >& /dev/null
	if [ $? -eq "1" ]; then
		echo "show dcb listeners:			Failed"
		failure=`expr $failure + 1`
	else
		passed=`expr $passed + 1`
		echo "show dcb listeners:			Passed"
	fi
done

sessions=`maxadmin -pskysql list sessions | awk -F\| '/Listener/ { if ( NF > 1 )  print $1 }'`
if [ $? -eq "1" ]; then
	echo "Get listener sessions:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Get listener sessions:			Passed"
fi

for i in $sessions
do
	maxadmin -pskysql show session $i | grep -s 'Listener' >& /dev/null
	if [ $? -eq "1" ]; then
		echo "show session listeners:			Failed"
		failure=`expr $failure + 1`
	else
		passed=`expr $passed + 1`
		echo "show session listeners:			Passed"
	fi
done

filters=`maxadmin -pskysql list filters | awk -F\| '{ if ( NF > 1 )  print $1 }'| grep -v Options`
if [ $? -eq "1" ]; then
	echo "Get Filter list:			Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Get filter list:			Passed"
fi

for i in $filters
do
	maxadmin -pskysql show filter $i | grep -s 'Filter' >& /dev/null
	if [ $? -eq "1" ]; then
		echo "show filter:				Failed"
		failure=`expr $failure + 1`
	else
		passed=`expr $passed + 1`
		echo "show filter:				Passed"
	fi
done

maxadmin -pskysql list services | \
	awk -F\| '{ if (NF > 1) { sub(/ +$/, "", $1); printf("show service \"%s\"\n", $1); } }' > script1.$$
grep -cs "show service" script1.$$ >/dev/null
if [ $? -ne "0" ]; then
	echo "list services:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "list services:				Passed"
fi
maxadmin -pskysql script1.$$ | grep -cs 'Service' > /dev/null
if [ $? -ne "0" ]; then
	echo "Show Service:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Show Service:				Passed"
fi
rm -f script1.$$


maxadmin -pskysql list monitors | \
	awk -F\| '{ if (NF > 1) { sub(/ +$/, "", $1); printf("show monitor \"%s\"\n", $1); } }' > script1.$$
grep -cs "show monitor" script1.$$ >/dev/null
if [ $? -ne "0" ]; then
	echo "list monitors:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "list monitors:				Passed"
fi
maxadmin -pskysql script1.$$ | grep -cs 'Monitor' > /dev/null
if [ $? -ne "0" ]; then
	echo "Show Monitor:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Show Monitor:				Passed"
fi
rm -f script1.$$


maxadmin -pskysql list sessions | \
	awk -F\| ' /^0x/ { if (NF > 1) { sub(/ +$/, "", $1); printf("show session \"%s\"\n", $1); } }' > script1.$$
grep -cs "show session" script1.$$ >/dev/null
if [ $? -ne "0" ]; then
	echo "list sessions:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "list sessions:				Passed"
fi
maxadmin -pskysql script1.$$ | grep -cs 'Session' > /dev/null
if [ $? -ne "0" ]; then
	echo "Show Session:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Show Session:				Passed"
fi
rm -f script1.$$


maxadmin -pskysql list dcbs | \
	awk -F\| ' /^ 0x/ { if (NF > 1) { sub(/ +$/, "", $1); sub(/ 0x/, "0x", $1); printf("show dcb \"%s\"\n", $1); } }' > script1.$$
grep -cs "show dcb" script1.$$ >/dev/null
if [ $? -ne "0" ]; then
	echo "list dcbs:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "list dcbs:				Passed"
fi
maxadmin -pskysql script1.$$ | grep -cs 'DCB' > /dev/null
if [ $? -ne "0" ]; then
	echo "Show DCB:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "Show DCB:				Passed"
fi
rm -f script1.$$


maxadmin -pskysql list services | \
	awk -F\| '{ if (NF > 1) { sub(/ +$/, "", $1); printf("show dbusers \"%s\"\n", $1); } }' > script1.$$
grep -cs "show dbusers" script1.$$ >/dev/null
if [ $? -ne "0" ]; then
	echo "list services:				Failed"
	failure=`expr $failure + 1`
else
	passed=`expr $passed + 1`
	echo "list services:				Passed"
fi
maxadmin -pskysql script1.$$ | grep -cs 'Users table data' > /dev/null
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
