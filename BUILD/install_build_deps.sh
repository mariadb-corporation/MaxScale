#!/bin/bash

# Do the real building work. This script is executed on build VM and
# requires a working installation of CMake.

command -v apt-get

if [ $? == 0 ]
then
  # DEB-based distro

  sudo apt-get update

  sudo apt-get install -y --force-yes dpkg-dev git gcc g++ ncurses-dev bison \
       build-essential libssl-dev libaio-dev perl make libtool libcurl4-openssl-dev \
       flex libeditline-dev uuid-dev liblzma-dev libpcre3-dev \
       liblua5.1 liblua5.1-dev
else
  ## RPM-based distro
  command -v yum

  if [ $? != 0 ]
  then
    # We need zypper here
    sudo zypper -n install gcc gcc-c++ ncurses-devel bison glibc-devel libgcc_s1 perl \
         make libtool libopenssl-devel libaio libaio-devel flex libcurl-devel \
         pcre-devel git wget libuuid-devel \
         xz-devel pkg-config lua lua-devel
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
         libedit-devel libcurl-devel curl-devel systemtap-sdt-devel rpm-sign \
         gnupg pcre-devel flex rpmdevtools git wget openssl libuuid-devel xz-devel \
         pkgconfig lua lua-devel rpm-build createrepo yum-utils 
    cat /etc/redhat-release | grep "release 5"
    if [ $? -eq 0 ]
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

# check CPU architecture to select proper MariaDB tarball
cat /proc/cpuinfo | grep cpu | grep POWER
if [ $? -ne 0 ] ; then

  dpkg --version
  if [ $? == 0 ] ; then
    dpkg -l | grep libc6
    export libc6_ver=`dpkg -l | sed "s/:amd64//g" |awk '$2=="libc6" { print $3 }'`
    dpkg --compare-versions $libc6_ver lt 2.14
    res=$?
  else
    export libc6_ver=`rpm --query glibc  --qf "%{VERSION}"`
    rpmdev-vercmp $libc6_ver 2.14
    if [ $? == 12 ] ; then
       res=0
    else
       res=1
    fi
    cat /etc/redhat-release | grep " 5\."
    if [ $? == 0 ] ; then
        res=0
    fi
    cat /etc/issue | grep "SUSE" | grep " 11 "
    if [ $? == 0 ] ; then
        res=0
    fi
  fi
set -x
  if [ $res != 0 ] ; then
    export mariadbd_link="https://downloads.mariadb.org/interstitial/mariadb-5.5.56/bintar-linux-glibc_214-x86_64/mariadb-5.5.56-linux-glibc_214-x86_64.tar.gz"
    export mariadbd_file="mariadb-5.5.56-linux-glibc_214-x86_64.tar.gz"
  else 
    export mariadbd_link="https://downloads.mariadb.org/interstitial/mariadb-5.5.56/bintar-linux-x86_64/mariadb-5.5.56-linux-x86_64.tar.gz"
    export mariadbd_file="mariadb-5.5.56-linux-x86_64.tar.gz"
  fi
else
        endian=`echo -n I | od -to2 | head -n1 | cut -f2 -d" " | cut -c6`
        if [ $endian == 0 ] ; then 
                export mariadbd_link="http://jenkins.engskysql.com/x/mariadb-5.5.41-linux-ppc64.tar.gz"
                export mariadbd_file="mariadb-5.5.41-linux-ppc64.tar.gz"
                cat /etc/redhat-release | grep " 6\."
                if [ $? == 0 ] ; then
                     export mariadbd_link="http://jenkins.engskysql.com/x/rhel6/mariadb-5.5.41-linux-ppc64.tar.gz"
                fi

        else
                export mariadbd_link="http://jenkins.engskysql.com/x/mariadb-5.5.41-linux-ppc64le.tar.gz"
                export mariadbd_file="mariadb-5.5.41-linux-ppc64le.tar.gz"
        fi
fi

wget --retry-connrefused $mariadbd_link
sudo tar xzvf $mariadbd_file -C /usr/ --strip-components=1
