#!/bin/sh

#This script unpacks the RPM to the provided directory

unpack_to(){
	cd $2 && rpm2cpio $1 | cpio -id;
}


if [[ $# -lt 2 ]]
then
	echo "Usage: unpack_rpm.sh <path to MariaDB RPMs> <installation directory>"
	exit 0
fi

SOURCE=$1
DEST=$2
FILES=$(ls $SOURCE |grep -i .*mariadb.*`uname -m`.*.rpm)

if [[ ! -d $DEST ]]
then
	mkdir -p $DEST
fi

echo "Unpacking RPMs to: $DEST"

for rpm in $FILES
do
	echo "Unpacking $rpm..."
	unpack_to $SOURCE/$rpm $DEST 
done

