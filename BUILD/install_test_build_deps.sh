#!/bin/bash

# MariaDB repo have to be configured and packages have to be installed 
# on the test machine e.g. with 'mdbci'
# mdbci install_product --product mariadb --product-version 10.6 test_vm/ubuntu_jammy
# mdbci install_product --product plugin_mariadb_test --product-version 10.6 test_vm/ubuntu_jammy
# mdbci install_product --product plugin_gssapi_client --product-version 10.6 test_vm/ubuntu_jammy

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

  ${apt_cmd} install \
       git wget build-essential libssl-dev \
       php perl \
       coreutils libjansson-dev zlib1g-dev \
       libsqlite3-dev libcurl4-gnutls-dev \
       python3 python3-pip cmake libpam0g-dev oathtool krb5-user \
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
else
  ## RPM-based distro
  install_libdir=/usr/lib64
  command -v yum

  if [ $? != 0 ]
  then
    # We need zypper here

    sudo zypper -n refresh
    sudo zypper -n install gcc gcc-c++ \
                 libopenssl-devel libgcrypt-devel MariaDB-devel \
                 php perl coreutils libjansson-devel \
                 cmake pam-devel openssl-devel libjansson-devel oath-toolkit \
                 sqlite3 sqlite3-devel libcurl-devel \
                 gnutls-devel \
                 libatomic1 \
                 cyrus-sasl-devel libxml2-devel krb5-devel
    sudo zypper -n install java-1_8_0-openjdk
    sudo zypper -n install php-mysql
  else
  # YUM!
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

    if yum repolist all | grep "^crb "
    then
        # RHEL 9 has the packages in the CRB repo
        enable_crb="--enablerepo=crb"
    fi
    sudo yum clean all
    sudo yum install -y --nogpgcheck ${enable_power_tools} ${enable_crb} epel-release
    sudo yum install -y --nogpgcheck ${enable_power_tools} ${enable_crb} \
                 git wget gcc gcc-c++ \
                 libgcrypt-devel \
                 openssl-devel mariadb-devel \
                 php perl coreutils  \
                 cmake pam-devel jansson-devel oathtool \
                 sqlite sqlite-devel libcurl-devel \
                 gnutls-devel \
                 libatomic \
                 cyrus-sasl-devel libxml2-devel krb5-devel
    sudo yum install -y --nogpgcheck ${enable_power_tools} ${enable_crb} java-1.8.0-openjdk
    sudo yum install -y --nogpgcheck ${enable_power_tools} ${enable_crb} centos-release-scl
    sudo yum install -y --nogpgcheck ${enable_power_tools} ${enable_crb} devtoolset-7-gcc*
    sudo yum install -y --nogpgcheck ${enable_power_tools} ${enable_crb} php-mysql
    echo "please run 'scl enable devtoolset-7 bash' to enable new gcc!!"
  fi
fi

# The tests need a very new CMake in order to produce JUnit XML output. This
# ends up installing CMake twice which isn't nice. The install_build_deps.sh
# script could be modified to take the CMake version as an argument which could
# then be passed down to install_cmake.sh.
$src_dir/install_cmake.sh "3.25.1"
