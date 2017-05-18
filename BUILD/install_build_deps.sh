#!/bin/bash

# Do the real building work. This script is executed on build VM and
# requires a working installation of CMake.

# Check if CMake needs to be installed
command -v cmake || install_cmake="cmake"


command -v apt-get

if [ $? -e 0 ]
then
  # DEB-based distro

  sudo apt-get update

  sudo apt-get install -y --force-yes dpkg-dev git gcc g++ ncurses-dev bison \
       build-essential libssl-dev libaio-dev perl make libtool libcurl4-openssl-dev \
       libpcre3-dev flex tcl libeditline-dev uuid-dev liblzma-dev libsqlite3-dev \
       sqlite3 liblua5.1 liblua5.1-dev libmicrohttpd-dev $install_cmake
else
  ## RPM-based distro
  command -v yum

  if [ $? -ne 0 ]
  then
    # We need zypper here
    sudo zypper -n install gcc gcc-c++ ncurses-devel bison glibc-devel libgcc_s1 perl \
         make libtool libopenssl-devel libaio libaio-devel flex libcurl-devel \
         pcre-devel git wget tcl libuuid-devel \
         xz-devel sqlite3 sqlite3-devel pkg-config lua lua-devel \
         libmicrohttpd-devel $install_cmake
    sudo zypper -n install rpm-build
    cat /etc/*-release | grep "SUSE Linux Enterprise Server 11"

    if [ $? -ne 0 ]
    then
      sudo zypper -n install libedit-devel
    fi
  else
    # YUM!
    sudo yum clean all
    sudo yum install -y --nogpgcheck gcc gcc-c++ ncurses-devel bison glibc-devel \
         libgcc perl make libtool openssl-devel libaio libaio-devel libedit-devel \
         libedit-devel libcurl-devel curl-devel systemtap-sdt-devel rpm-sign \
         gnupg pcre-devel flex rpmdevtools git wget tcl openssl libuuid-devel xz-devel \
         sqlite sqlite-devel pkgconfig lua lua-devel rpm-build createrepo yum-utils \
         libmicrohttpd-devel $install_cmake

    cat /etc/redhat-release | grep "release 5"
    if [ $? -eq 0 ]
    then
      sudo yum remove -y libedit-devel libedit
    fi
  fi

fi

# Flex
wget http://maxscale-jenkins.mariadb.com/x/flex-2.5.35-0.8.el5.rfb.x86_64.rpm
sudo yum install flex-2.5.35-0.8.el5.rfb.x86_64.rpm -y --nogpgcheck
rm flex-2.5.35-0.8.el5.rfb.x86_64*

# RabbitMQ C client
mkdir rabbit
cd rabbit
git clone https://github.com/alanxz/rabbitmq-c.git

if [ $? -ne 0 ]
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

if [ $? -ne 0 ]
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

mkdir -p jansson/build
pushd jansson/build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_C_FLAGS=-fPIC -DJANSSON_INSTALL_LIB_DIR=/usr/lib64
make
sudo make install
popd

# Avro C API
wget -r -l1 -nH --cut-dirs=2 --no-parent -A.tar.gz --no-directories http://mirror.netinch.com/pub/apache/avro/stable/c
if [ $? != 0 ]
then
    echo "Error getting avro-c"
    exit 1
fi
avro_filename=`ls -1 *.tar.gz`
avro_die=`echo "$avro_finema" | sed "s/.tar.gz//"`

tar -axf $avro_filename
mkdir $avro_dir/build
pushd $avro_dir/build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_C_FLAGS=-fPIC -DCMAKE_CXX_FLAGS=-fPIC
make
sudo make install
popd
