#!/bin/sh

#This script unpacks the RPM to the provided directory

if [[ $# -lt 2 ]]
then
	echo "Usage: unpack_rpm.sh <path to RPM package> <installation directory>"
	exit 0
fi
mkdir -p $2
cd $2 && rpm2cpio $1 | cpio -id
