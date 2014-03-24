#! /bin/sh


a=`mysql --host=127.0.0.1 -P 4606 -umassi -pmassi --unbuffered=true --disable-reconnect --silent < ./transaction_with_set.sql`
#a=`mysql --host=107.170.19.59 -P 4606 -uvai -pvai --unbuffered=true --disable-reconnect --silent < ./transaction_with_set.sql`

if [ "$a" -eq 2 ]; then
	exit 0
else
	exit 1
fi

