#!/bin/bash

# Installs all build dependencies for system tests
# Only Ubuntu Bionic/Xenial, CentOS 7, SLES 15 are supported

rp=`realpath $0`
export src_dir=`dirname $rp`
export LC_ALL=C
command -v apt-get

if [ $? == 0 ]
then
  # DEB-based distro
  export DEBIAN_FRONTEND=noninteractive
  install_libdir=/usr/lib
  source /etc/os-release
  echo "deb http://mirror.netinch.com/pub/mariadb/repo/10.3/ubuntu/ ${UBUNTU_CODENAME} main" > mariadb.list
  sudo cp mariadb.list /etc/apt/sources.list.d/
  sudo apt-key adv --keyserver keyserver.ubuntu.com --recv-keys 0xF1656F24C74CD1D8
  export DEBIAN_FRONTEND=noninteractive
  apt_cmd="sudo -E apt-get -q -o Dpkg::Options::=--force-confold \
       -o Dpkg::Options::=--force-confdef \
       -y --force-yes"
  ${apt_cmd} update
  ${apt_cmd} install \
       git wget build-essential \
       libssl-dev mariadb-client php perl \
       coreutils libjansson-dev zlib1g-dev \
       libsqlite3-dev libcurl4-gnutls-dev \
       mariadb-test python python-pip cmake libpam0g-dev oathtool \
       libatomic1 \
       libsasl2-dev libxml2-dev libkrb5-dev

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

  ${apt_cmd} install php-mysql
  ${apt_cmd} install openjdk-8-jdk
  if [ $? != 0 ]
  then
      ${apt_cmd} install openjdk-7-jdk
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
                 cmake pam-devel openssl-devel python-devel libjansson-devel oath-toolkit \
                 sqlite3 sqlite3-devel libcurl-devel \
                 gnutls-devel \
                 libatomic1 \
                 cyrus-sasl-devel libxml2-devel krb5-devel
    sudo zypper -n install java-1_8_0-openjdk
    sudo zypper -n install php-mysql
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
                 cmake pam-devel python-devel jansson-devel oathtool \
                 sqlite sqlite-devel libcurl-devel \
                 gnutls-devel \
                 libatomic \
                 cyrus-sasl-devel libxml2-devel krb5-devel
    sudo yum install -y --nogpgcheck java-1.8.0-openjdk
    sudo yum install -y --nogpgcheck centos-release-scl
    sudo yum install -y --nogpgcheck devtoolset-7-gcc*
    sudo yum install -y --nogpgcheck php-mysql
    echo "please run 'scl enable devtoolset-7 bash' to enable new gcc!!"
  fi
  sudo pip install --upgrade pip
  sudo pip install JayDeBeApi
fi

# Install a recent cmake in case the package manager installed an old version.
$src_dir/install_cmake.sh

# Install NPM for MongoDB tests
$src_dir/install_npm.sh
