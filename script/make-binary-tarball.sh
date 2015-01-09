#!/bin/sh


read -p "Enter path where MaxScale is installed:" instpath
if [ "${instpath}" = "" ]; then
		echo "Error: input path is null, exit"
		exit 1
fi

BINARY_PATH=${instpath}
cd ${BINARY_PATH}
BINARY_PATH=${PWD}
echo "Looking for MaxScale in [${BINARY_PATH}]"

if [ -s "${BINARY_PATH}/bin/maxscale" ]; then
	if [ -x "${BINARY_PATH}/bin/maxscale" ]; then
		MAXSCALE_VERSION=`strings ${BINARY_PATH}/bin/maxscale | grep "MariaDB Corporation MaxScale" | awk '{print $3}' | head -1`
		echo "Found MaxScale, version: ${MAXSCALE_VERSION}"
	fi
else
	echo "Error: MaxScale was not found!"
	exit 1
fi

MAXSCALE_BINARY_TARFILE=maxscale.${MAXSCALE_VERSION}.tar
TARFILE_BASEDIR=maxscale-${MAXSCALE_VERSION}
TARFILE_BASEDIR_SUBST='s,^\.,'${TARFILE_BASEDIR}','

rm -rf ${MAXSCALE_BINARY_TARFILE}.gz
rm -rf ${MAXSCALE_BINARY_TARFILE}

TARFILE_BASEDIR_SUBST='s,^'${BINARY_PATH}','${TARFILE_BASEDIR}','

tar --absolute-names --owner=maxscale --group=maxscale --transform=${TARFILE_BASEDIR_SUBST} -cf ${MAXSCALE_BINARY_TARFILE} ${BINARY_PATH}/*
gzip ${MAXSCALE_BINARY_TARFILE}

if [ -s "${MAXSCALE_BINARY_TARFILE}.gz" ]; then
	echo "File ["${MAXSCALE_BINARY_TARFILE}".gz] is ready in ["$BINARY_PATH"]"
else
	echo "Error: File ["${MAXSCALE_BINARY_TARFILE}".gz] was not created in ["$BINARY_PATH"]"
fi
