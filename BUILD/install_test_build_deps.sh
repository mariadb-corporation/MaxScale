#!/bin/bash

scriptdir=$(dirname $(realpath $0))

# Install all build dependences
# TODO: Remove unnecessary stuff from this script.
${scriptdir}/install_build_deps.sh

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
  apt_cmd="sudo -E apt-get -q -o Dpkg::Options::=--force-confold \
       -o Dpkg::Options::=--force-confdef -y"
  ${apt_cmd} update

  ${apt_cmd} install curl
  curl -LsS https://r.mariadb.com/downloads/mariadb_repo_setup | sudo bash

  ${apt_cmd} install \
       git wget build-essential libssl-dev \
       mariadb-client mariadb-plugin-gssapi-client \
       php perl \
       coreutils libjansson-dev zlib1g-dev \
       libsqlite3-dev libcurl4-gnutls-dev \
       mariadb-test python3 python3-pip cmake libpam0g-dev oathtool krb5-user \
       libatomic1 \
       libsasl2-dev libkrb5-dev unixodbc-dev

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

  # Installing maven installs the correct version of Java as a dependency
  ${apt_cmd} install maven
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
                 php perl coreutils libjansson-devel \
                 cmake pam-devel openssl-devel libjansson-devel oath-toolkit \
                 sqlite3 sqlite3-devel libcurl-devel \
                 gnutls-devel \
                 libatomic1 \
                 cyrus-sasl-devel krb5-devel unixODBC-devel
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
                 php perl coreutils  \
                 cmake pam-devel jansson-devel oathtool \
                 sqlite sqlite-devel libcurl-devel \
                 gnutls-devel \
                 libatomic \
                 cyrus-sasl-devel krb5-devel unixODBC-devel
    # Installing maven installs the correct version of Java as a dependency
    sudo yum install -y --nogpgcheck maven
    sudo yum install -y --nogpgcheck centos-release-scl
    sudo yum install -y --nogpgcheck devtoolset-7-gcc*
    sudo yum install -y --nogpgcheck php-mysql
    echo "please run 'scl enable devtoolset-7 bash' to enable new gcc!!"
  fi
fi

# The tests need a very new CMake in order to produce JUnit XML output. This
# ends up installing CMake twice which isn't nice. The install_build_deps.sh
# script could be modified to take the CMake version as an argument which could
# then be passed down to install_cmake.sh.
$src_dir/install_cmake.sh "3.25.1"
