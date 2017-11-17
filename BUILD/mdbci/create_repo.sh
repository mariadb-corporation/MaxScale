#!/bin/bash

set -x

destdir=$1
sourcedir=$2

#rm -rf $destdir
mkdir -p  $destdir/

zypper --version
z_res=$?
yum --version
y_res=$?

if [ $z_res -eq 127 ] && [ $y_res -eq 127 ] ; then
# DEB-based system
	arch_name=`dpkg --print-architecture`
	arch="binary-$arch_name"
	cd $destdir
	debian_ver=`cat /etc/debian_version`
	echo "Debian version: $debian_ver"
	dist_name=$platform_version

	mkdir -p dists/$dist_name/main/$arch/

	cp ~/$sourcedir/* dists/$dist_name/main/$arch/
	sudo apt-get update
	sudo apt-get install -y dpkg-dev
	dpkg-scanpackages dists/$dist_name/main/$arch/  /dev/null | gzip -9c > dists/$dist_name/main/$arch/Packages.gz
	gunzip -c dists/$dist_name/main/$arch/Packages.gz > dists/$dist_name/main/$arch/Packages
#	echo "Archive: main" > dists/$dist_name/main/$arch/Release
#	echo "Suite: main" >> dists/$dist_name/main/$arch/Release
	echo "Components: main" >> dists/$dist_name/main/$arch/Release
	echo "Codename: $dist_name" >> dists/$dist_name/main/$arch/Release
	echo "Origin: MariaDB" >> dists/$dist_name/main/$arch/Release
	echo "Label: MariaDB Maxscale repository" >> dists/$dist_name/main/$arch/Release
	uname -m | grep "x86_64"
	if [ $? -eq 0 ] ; then
# 		echo "Architectures: amd64 i386" >> dists/$dist_name/main/$arch/Release
		mkdir -p dists/$dist_name/main/binary-i386/
		dpkg-scanpackages dists/$dist_name/main/binary-i386/  /dev/null | gzip -9c > dists/$dist_name/main/binary-i386/Packages.gz
	        gunzip -c dists/$dist_name/main/binary-i386/Packages.gz > dists/$dist_name/main/binary-i386/Packages
#	else 
#		 echo "Architectures: ppc64el" >> dists/$dist_name/main/$arch/Release
	fi
	archs=`ls -1 dists/$dist_name/main | sed "s/binary-//" | tr '\n' ' '`
	echo "Architectures: $archs" >> dists/$dist_name/main/$arch/Release
	echo "Description:  MariaDB MaxScale" >> dists/$dist_name/main/$arch/Release
	cp dists/$dist_name/main/$arch/Release dists/$dist_name/Release
#	cp dists/$dist_name/main/$arch/Packages.gz dists/$dist_name
	apt-ftparchive release dists/$dist_name/ >> dists/$dist_name/Release
        if [ $? != 0 ] ; then
                echo "Repo creation failed!"
                exit 1
        fi

	gpg -abs -o  dists/$dist_name/Release.gpg dists/$dist_name/Release 
	if [ $? != 0 ] ; then
		echo "Package signing failed!"
		exit 1
	fi
else
# RPM-based system
	sudo yum install -y createrepo
	sudo zypper -n remove patterns-openSUSE-minimal_base-conflicts
	sudo zypper -n install createrepo
	echo "%_signature gpg" >> ~/.rpmmacros
        echo "%_gpg_name  MariaDB Maxscale" >>  ~/.rpmmacros
#	echo "%_gpg_name  MariaDBManager" >>  ~/.rpmmacros
	echo "\r" |  setsid rpm --resign $sourcedir/*.rpm
	
        if [ $? != 0 ] ; then
                echo "Package signing failed!"
                exit 1
        fi
	gpg --output repomd.xml.key --sign $sourcedir/repodata/repomd.xml
	cp $sourcedir/* $destdir/
	pushd ${destdir} >/dev/null 2>&1
	    createrepo -d -s sha .
	        if [ $? != 0 ] ; then
        	        echo "Repo creation failed!"
                	exit 1
	        fi	

	popd >/dev/null 2>&1
	gpg -a --detach-sign $destdir/repodata/repomd.xml
        if [ $? != 0 ] ; then
                echo "Package signing failed!"
                exit 1
        fi
fi
