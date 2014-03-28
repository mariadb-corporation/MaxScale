#!/bin/sh

SOURCE_PATH=${PWD}/..

cd ${SOURCE_PATH}

SOURCE_PATH=${PWD}

read -p "Building source tarball from ${SOURCE_PATH} ? [y/n]" yn

case $yn in
        [Yy]* ) 
		break

		;;
        [Nn]* ) read -p "Enter MaxScale source tree path: " new_path
		if [ "${new_path}" = "" ]; then
			echo "Error: input path null, exit"
			exit 1
		fi
		SOURCE_PATH=$new_path
		cd ${SOURCE_PATH}
		echo "Selected source tree is [$new_path]"
		break

		;;
        * ) echo "Please answer yes or no!"
		exit 1

		;;
esac


if [ -s "./VERSION" ]; then
	MAXSCALE_VERSION=`cat ./VERSION`
	echo "MaxScale version:" ${MAXSCALE_VERSION}
else
	echo "Error: MaxScale version file ./VERSION not found!"
	exit 1
fi

MAXSCALE_SOURCE_TARFILE=maxscale.src.${MAXSCALE_VERSION}.tar
TARFILE_BASEDIR=maxscale-${MAXSCALE_VERSION}
TARFILE_BASEDIR_SUBST='s,^\.,'${TARFILE_BASEDIR}','

rm -rf ${MAXSCALE_SOURCE_TARFILE}.gz
rm -rf ${MAXSCALE_SOURCE_TARFILE}

TARFILE_BASEDIR_SUBST='s,^'${SOURCE_PATH}','${TARFILE_BASEDIR}','

tar --absolute-names --owner=maxscale --group=maxscale --transform=${TARFILE_BASEDIR_SUBST} -cf ${MAXSCALE_SOURCE_TARFILE} ${SOURCE_PATH}/*
gzip ${MAXSCALE_SOURCE_TARFILE}

if [ -s "${MAXSCALE_SOURCE_TARFILE}.gz" ]; then
	echo "File ["${MAXSCALE_SOURCE_TARFILE}".gz] is ready in ["$SOURCE_PATH"]"
else
	echo "Error: File ["${MAXSCALE_SOURCE_TARFILE}".gz] was not created in ["$SOURCE_PATH"]"
fi
