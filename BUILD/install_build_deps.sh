#!/bin/bash

# Do the real building work. This script is executed on build VM and
# requires a working installation of CMake.


command -v apt-get

if [ $? == 0 ]
then
  # DEB-based distro
  install_libdir=/usr/lib
  sudo apt-get update

  sudo apt-get install -y --force-yes dpkg-dev git wget \
       build-essential libssl-dev ncurses-dev bison flex \
       perl libtool libpcre3-dev tcl tcl-dev uuid \
       uuid-dev libsqlite3-dev liblzma-dev libpam0g-dev
  ## separatelibgnutls installation process for Ubuntu Trusty
  cat /etc/*release | grep -E "Trusty|wheezy"
  if [ $? == 0 ]
  then
     sudo apt-get install -y --force-yes libgnutls-dev libgcrypt11-dev
  else
     sudo apt-get install -y --force-yes libgnutls30 libgnutls-dev
     if [ $? != 0 ]
     then
         sudo apt-get install -y --force-yes libgnutls28-dev
     fi
     sudo apt-get install -y --force-yes libgcrypt20-dev
     if [ $? != 0 ]
     then
         sudo apt-get install -y --force-yes libgcrypt11-dev
     fi
  fi
else
  ## RPM-based distro
  install_libdir=/usr/lib64
  command -v yum

  if [ $? != 0 ]
  then
    # We need zypper here
    sudo zypper -n install gcc gcc-c++ ncurses-devel bison glibc-devel libgcc_s1 perl \
         make libtool libopenssl-devel libaio libaio-devel flex \
         pcre-devel git wget tcl libuuid-devel \
         xz-devel sqlite3 sqlite3-devel pkg-config lua lua-devel \
         gnutls-devel libgcrypt-devel pam-devel
    sudo zypper -n install rpm-build
    cat /etc/*-release | grep "SUSE Linux Enterprise Server 11"

    if [ $? != 0 ]
    then
      sudo zypper -n install libedit-devel
    fi
  else
    # YUM!
    sudo yum clean all
    sudo yum install -y --nogpgcheck gcc gcc-c++ ncurses-devel bison glibc-devel \
         libgcc perl make libtool openssl-devel libaio libaio-devel libedit-devel \
         libedit-devel systemtap-sdt-devel rpm-sign wget \
         gnupg pcre-devel flex rpmdevtools git wget tcl openssl libuuid-devel xz-devel \
         sqlite sqlite-devel pkgconfig lua lua-devel rpm-build createrepo yum-utils \
         gnutls-devel libgcrypt-devel pam-devel

    cat /etc/redhat-release | grep "release 5"
    if [ $? == 0 ]
    then
      sudo yum remove -y libedit-devel libedit
    fi
  fi

fi

# cmake
wget http://max-tst-01.mariadb.com/ci-repository/cmake-3.7.1-Linux-x86_64.tar.gz --no-check-certificate
if [ $? != 0 ] ; then
    echo "CMake can not be downloaded from Maxscale build server, trying from cmake.org"
    wget https://cmake.org/files/v3.7/cmake-3.7.1-Linux-x86_64.tar.gz --no-check-certificate
fi
sudo tar xzvf cmake-3.7.1-Linux-x86_64.tar.gz -C /usr/ --strip-components=1

cmake_version=`cmake --version | grep "cmake version" | awk '{ print $3 }'`
if [ "$cmake_version" \< "3.7.1" ] ; then
    echo "cmake does not work! Trying to build from source"
    wget https://cmake.org/files/v3.7/cmake-3.7.1.tar.gz --no-check-certificate
    tar xzvf cmake-3.7.1.tar.gz
    cd cmake-3.7.1

    ./bootstrap
    gmake
    sudo make install
    cd ..
fi

# RabbitMQ C client
mkdir rabbit
cd rabbit
git clone https://github.com/alanxz/rabbitmq-c.git

if [ $? != 0 ]
then
    echo "Error cloning rabbitmq-c"
    exit 1
fi

cd rabbitmq-c
git checkout v0.7.1
cmake .  -DCMAKE_C_FLAGS=-fPIC -DBUILD_SHARED_LIBS=N  -DCMAKE_INSTALL_PREFIX=/usr
sudo make install
cd ../../

# TCL
mkdir tcl
cd tcl
wget --no-check-certificate http://prdownloads.sourceforge.net/tcl/tcl8.6.5-src.tar.gz

if [ $? != 0 ]
then
    echo "Error getting tcl"
    exit 1
fi

tar xzvf tcl8.6.5-src.tar.gz
cd tcl8.6.5/unix
./configure
sudo make install
cd ../../..


# Jansson
git clone https://github.com/akheron/jansson.git
if [ $? != 0 ]
then
    echo "Error cloning jansson"
    exit 1
fi

cd jansson
git checkout v2.9
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_C_FLAGS=-fPIC -DJANSSON_INSTALL_LIB_DIR=$install_libdir
make
sudo make install
cd ../../

# Avro C API
wget -r -l1 -nH --cut-dirs=2 --no-parent -A.tar.gz --no-directories http://mirror.netinch.com/pub/apache/avro/stable/c
if [ $? != 0 ]
then
    echo "Error getting avro-c"
    exit 1
fi
avro_filename=`ls -1 avro*.tar.gz`
avro_dir=`echo "$avro_filename" | sed "s/.tar.gz//"`
tar -axf $avro_filename
mkdir $avro_dir/build
pushd $avro_dir/build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_C_FLAGS=-fPIC -DCMAKE_CXX_FLAGS=-fPIC
make
sudo make install
popd

wget --quiet https://nodejs.org/dist/v6.11.2/node-v6.11.2-linux-x64.tar.xz
tar -axf node-v6.11.2-linux-x64.tar.xz
sudo cp -t /usr -r node-v6.11.2-linux-x64/*
