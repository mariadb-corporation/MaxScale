#!/bin/bash

# Do the real building work. This script is executed on build VM.

# Build in a temp directory so we don't pollute cwd
tmpdir=$(mktemp -d)
scriptdir=$(dirname $(realpath $0))

cd $tmpdir

distro_id=`cat /etc/*-release | grep "^ID_LIKE=" | sed "s/ID=//"`

function is_arm() {
    [ "$(arch)" == "aarch64" ]
}

unset packager_type

if [[ ${distro_id} =~ "suse" ]]
then
   packager_type="zypper"
fi

if [[ ${distro_id} =~ "rhel" ]]
then
   packager_type="yum"
fi

if [[ ${distro_id} =~ "debian" ]]
then
   packager_type="apt"
fi

if [[ ${packager_type} == "" ]]
then
    command -v apt-get

    if [ $? == 0 ]
    then
        packager_type="apt"
    fi

    command -v yum

    if [ $? == 0 ]
    then
        packager_type="yum"
    fi

    command -v zypper

    if [ $? == 0 ]
    then
        packager_type="zypper"
    fi
fi

if [[ ${packager_type} == "" ]]
then
    echo "Can not determine package manager type, exiting"
    exit 1
fi

if [[ ${packager_type} == "apt" ]]
then
  # DEB-based distro
  install_libdir=/usr/lib
  export DEBIAN_FRONTEND=noninteractive
  sudo apt-get update

  sudo dpkg-reconfigure libc6
  apt_cmd="sudo -E apt-get -q -o Dpkg::Options::=--force-confold \
       -o Dpkg::Options::=--force-confdef \
       -y --force-yes"
  ${apt_cmd} install dpkg-dev git wget cmake \
       build-essential libssl-dev ncurses-dev bison flex \
       perl libtool tcl tcl-dev uuid \
       uuid-dev libsqlite3-dev liblzma-dev libpam0g-dev pkg-config \
       libedit-dev libcurl4-openssl-dev libatomic1 \
       libsasl2-dev libxml2-dev libkrb5-dev libicu-dev

  # One of these will work, older systems use libsystemd-daemon-dev
  ${apt_cmd} install libsystemd-dev || \
      ${apt_cmd} install libsystemd-daemon-dev

  if is_arm
  then
     # Some OS versions on ARM require Python to build stuff, mostly for nodejs related stuff
     ${apt_cmd} install python3
  fi

   ## separate libgnutls installation process for Ubuntu Trusty
  cat /etc/*release | grep -E "Trusty|wheezy"
  if [ $? == 0 ]
  then
     ${apt_cmd} install libgnutls-dev libgcrypt11-dev
  else
     ${apt_cmd} install libgnutls30 libgnutls-dev
     if [ $? != 0 ]
     then
         ${apt_cmd} install libgnutls28-dev
     fi
     ${apt_cmd} install libgcrypt20-dev
     if [ $? != 0 ]
     then
         ${apt_cmd} install libgcrypt11-dev
     fi
  fi
fi

if [[ ${packager_type} == "yum" ]]
then
    install_libdir=/usr/lib64
    # YUM!
    sudo yum clean all
    sudo yum update -y
    unset enable_power_tools
    yum repolist all | grep "^PowerTools"
    if [ $? == 0 ]
    then
        enable_power_tools="--enablerepo=PowerTools"
    fi
    yum repolist all | grep "^powertools"
    if [ $? == 0 ]
    then
        enable_power_tools="--enablerepo=powertools"
    fi
    sudo yum install -y --nogpgcheck ${enable_power_tools} \
         gcc gcc-c++ ncurses-devel bison glibc-devel cmake \
         libgcc perl make libtool openssl-devel libaio libaio-devel  \
         systemtap-sdt-devel rpm-sign \
         gnupg flex rpmdevtools git wget tcl tcl-devel openssl libuuid-devel xz-devel \
         sqlite sqlite-devel pkgconfig rpm-build createrepo yum-utils \
         gnutls-devel libgcrypt-devel pam-devel libcurl-devel libatomic \
         cyrus-sasl-devel libxml2-devel krb5-devel libicu-devel

    sudo yum install -y --nogpgcheck ${enable_power_tools} lua lua-devel libedit-devel

    # Attempt to install systemd-devel, doesn't work on CentOS 6
    sudo yum install -y systemd-devel

    if is_arm
    then
       # Some OS versions on ARM require Python to build stuff, mostly for nodejs related stuff
       sudo yum -y install python3
    fi

    # Enable the devtoolkit to get a newer compiler

    # CentOS: install the centos-release-scl repo
    # RHEL: enable the existing repo (seems to be rhui-REGION-rhel-server-rhscl on AWS)
    sudo yum -y install centos-release-scl || \
        sudo yum-config-manager --enable rhui-REGION-rhel-server-rhscl

    # Install newer compiler for CentOS 7 and 6
    grep "release [67]" /etc/redhat-release
    if [ $? -eq 0 ]
    then
        sudo yum -y install devtoolset-7-gcc-c++
        sudo yum -y install devtoolset-7-libasan-devel
        # Enable it by default
        echo "source /opt/rh/devtoolset-7/enable" >> ~/.bashrc
    else
        # CentOS 8 only needs ASAN
        sudo yum -y install libasan-devel
    fi

    grep "release [78]" /etc/redhat-release
    if [ $? -eq 0 ]
    then
        # EPEL is installed for GCOV report generation (lcov)
        sudo yum -y install epel-release
        sudo yum -y install lcov
    fi
fi

if [[ ${packager_type} == "zypper" ]]
then
    install_libdir=/usr/lib64
    # We need zypper here
    sudo zypper -n refresh
    sudo zypper -n update
    sudo zypper -n remove gettext-runtime-mini
    sudo zypper -n install gcc gcc-c++ cmake ncurses-devel bison glibc-devel libgcc_s1 perl \
         make libtool libopenssl-devel libaio libaio-devel flex \
         git wget tcl tcl-devel libuuid-devel \
         xz-devel sqlite3 sqlite3-devel pkg-config lua lua-devel \
         gnutls-devel libgcrypt-devel pam-devel systemd-devel libcurl-devel libatomic1 \
         cyrus-sasl-devel libxml2-devel krb5-devel libicu-devel
    sudo zypper -n install rpm-build
    cat /etc/*-release | grep "SUSE Linux Enterprise Server 11"

    if [ $? != 0 ]
    then
      sudo zypper -n install libedit-devel
    fi

    if is_arm
    then
       # Some OS versions on ARM require Python to build stuff, mostly for nodejs related stuff
       sudo zypper -n install python3
    fi

    # Install a newer compiler
    sudo zypper -n install gcc9 gcc9-c++
    if [ $? == 0 ]
    then
        echo "export CC=/usr/bin/gcc-9" >> ~/.bashrc
        echo "export CXX=/usr/bin/g++-9" >> ~/.bashrc
    fi
    sudo zypper -n install gcc10 gcc10-c++
    if [ $? == 0 ]
    then
        echo "export CC=/usr/bin/gcc-10" >> ~/.bashrc
        echo "export CXX=/usr/bin/g++-10" >> ~/.bashrc
    fi
fi

# Methods allow to compare software versions according to semantic versioning
verlte() {
    [  "$1" = "`echo -e "$1\n$2" | sort -V | head -n1`" ]
}

verlt() {
    [ "$1" = "$2" ] && return 1 || verlte $1 $2
}

# Install a recent cmake in case the package manager installed an old version.
$scriptdir/install_cmake.sh

# TCL
system_tcl_version=$(tclsh <<< 'puts [info patchlevel]')
if verlt "$system_tcl_version" "8.6.5"
then
   mkdir tcl
   cd tcl
   wget --quiet --no-check-certificate http://prdownloads.sourceforge.net/tcl/tcl8.6.5-src.tar.gz

   if [ $? != 0 ]
   then
       echo "Error getting tcl"
       exit 1
   fi

   tar xzf tcl8.6.5-src.tar.gz
   cd tcl8.6.5/unix
   ./configure
   sudo make install
   cd ../../..
fi


# Install NPM for MaxCtrl and the GUI
$scriptdir/install_npm.sh

sudo rm -rf $tmpdir
