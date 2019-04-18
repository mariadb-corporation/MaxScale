#!/bin/bash

# Installs all build dependecies for maxscale-system-test
# Only Ubuntu Bionic/Xenial, CentOS 7, SLES 15 are supported

rp=`realpath $0`
export src_dir=`dirname $rp`
export LC_ALL=C
command -v apt-get

if [ $? == 0 ]
then
  # DEB-based distro
  install_libdir=/usr/lib
  source /etc/os-release
  echo "deb http://mirror.netinch.com/pub/mariadb/repo/10.3/ubuntu/ ${UBUNTU_CODENAME} main" > mariadb.list
  sudo cp mariadb.list /etc/apt/sources.list.d/
  sudo apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 0xF1656F24C74CD1D8
  sudo apt-get update
  sudo apt-get install -y --force-yes \
                 git wget build-essential \
                 libssl-dev mariadb-client php perl \
                 coreutils libjansson-dev zlib1g-dev \
                 mariadb-test python python-pip cmake libpam0g-dev
  sudo apt-get install -y --force-yes openjdk-8-jdk
  if [ $? != 0 ]
  then
    sudo apt-get install -y --force-yes openjdk-7-jdk
  fi
  pip install --upgrade pip
  pip install JayDeBeApi
else
  ## RPM-based distro
  install_libdir=/usr/lib64
  command -v yum

  if [ $? != 0 ]
  then
    # We need zypper here
    cat >mariadb.repo <<'EOL'
[mariadb]
name = MariaDB
baseurl = http://yum.mariadb.org/10.3/sles/$releasever/$basearch/
gpgkey=https://yum.mariadb.org/RPM-GPG-KEY-MariaDB
gpgcheck=0
EOL
    sudo cp mariadb.repo /etc/zypp/repos.d/

    sudo zypper -n refresh
    sudo zypper -n install gcc gcc-c++ \
                 libopenssl-devel libgcrypt-devel MariaDB-devel MariaDB-test \
                 php perl coreutils libjansson-devel python python-pip \
                 cmake pam-devel openssl-devel python-devel libjansson-devel
    sudo zypper -n install java-1_8_0-openjdk
  else
  # YUM!
    cat >mariadb.repo <<'EOL'
[mariadb]
name = MariaDB
baseurl = http://yum.mariadb.org/10.3/centos/$releasever/$basearch/
gpgkey=https://yum.mariadb.org/RPM-GPG-KEY-MariaDB
gpgcheck=0
EOL
    sudo cp mariadb.repo /etc/yum.repos.d/
    sudo yum clean all
    sudo yum install -y --nogpgcheck epel-release
    sudo yum install -y --nogpgcheck git wget gcc gcc-c++ \
                 libgcrypt-devel \
                 openssl-devel mariadb-devel mariadb-test \
                 php perl coreutils python python-pip \
                 cmake pam-devel python-devel jansson-devel
    sudo yum install -y --nogpgcheck java-1.8.0-openjdk
    sudo yum install -y --nogpgcheck centos-release-scl
    sudo yum install -y --nogpgcheck devtoolset-7-gcc*
    echo "please run 'scl enable devtoolset-7 bash' to enable new gcc!!"
  fi
  sudo pip install --upgrade pip
  sudo pip install JayDeBeApi
fi

