#!/bin/bash

# Do the real building work. This script is executed on build VM and
# requires a working installation of CMake.

# Build in a temp directory so we don't pollute cwd
tmpdir=$(mktemp -d)

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
  ${apt_cmd} install dpkg-dev git wget \
       build-essential libssl-dev ncurses-dev bison flex \
       perl libtool tcl tcl-dev uuid \
       uuid-dev libsqlite3-dev liblzma-dev libpam0g-dev pkg-config \
       libedit-dev libcurl4-openssl-dev libatomic1 \
       libsasl2-dev libxml2-dev libkrb5-dev

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
         gcc gcc-c++ ncurses-devel bison glibc-devel \
         libgcc perl make libtool openssl-devel libaio libaio-devel  \
         systemtap-sdt-devel rpm-sign \
         gnupg flex rpmdevtools git wget tcl tcl-devel openssl libuuid-devel xz-devel \
         sqlite sqlite-devel pkgconfig rpm-build createrepo yum-utils \
         gnutls-devel libgcrypt-devel pam-devel libcurl-devel libatomic \
         cyrus-sasl-devel libxml2-devel krb5-devel

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
    sudo zypper -n install gcc gcc-c++ ncurses-devel bison glibc-devel libgcc_s1 perl \
         make libtool libopenssl-devel libaio libaio-devel flex \
         git wget tcl tcl-devel libuuid-devel \
         xz-devel sqlite3 sqlite3-devel pkg-config lua lua-devel \
         gnutls-devel libgcrypt-devel pam-devel systemd-devel libcurl-devel libatomic1 \
         cyrus-sasl-devel libxml2-devel krb5-devel
    sudo zypper -n install rpm-build
    cat /etc/*-release | grep "SUSE Linux Enterprise Server 11"

    if is_arm
    then
       # Some OS versions on ARM require Python to build stuff, mostly for nodejs related stuff
       sudo zypper -n install python3
    fi

    if [ $? != 0 ]
    then
      sudo zypper -n install libedit-devel
    fi

    # Install a newer compiler
    sudo zypper -n install gcc9 gcc9-c++
    echo "export CC=/usr/bin/gcc-9" >> ~/.bashrc
    echo "export CXX=/usr/bin/g++-9" >> ~/.bashrc
fi

# Methods allow to compare software versions according to semantic versioning
verlte() {
    [  "$1" = "`echo -e "$1\n$2" | sort -V | head -n1`" ]
}

verlt() {
    [ "$1" = "$2" ] && return 1 || verlte $1 $2
}

# cmake
cmake_vrs_cmd="cmake --version"
cmake_version_ok=0
cmake_version_required="3.16.0"
if command -v ${cmake_vrs_cmd} &> /dev/null ; then
  cmake_version=`${cmake_vrs_cmd} | grep "cmake version" | awk '{ print $3 }'`
  if verlt $cmake_version $cmake_version_required ; then
    echo "Found CMake ${cmake_version}, which is too old."
  else
    cmake_version_ok=1
    echo "Found CMake ${cmake_version}, which is recent enough."
  fi
else
  echo "CMake not found"
fi

if [ $cmake_version_ok -eq 0 ] ; then

    if is_arm
    then
        cmake_version="3.20.2"
        cmake_filename="cmake-${cmake_version}-linux-aarch64.tar.gz"
        wget https://github.com/Kitware/CMake/releases/download/v${cmake_version}/${cmake_filename}
        sudo tar -axf ${cmake_filename} -C /usr/ --strip-components=1
    else
        cmake_filename="cmake-3.16.8-Linux-x86_64.tar.gz"
        wget -q http://max-tst-01.mariadb.com/ci-repository/${cmake_filename} --no-check-certificate
        if [ $? != 0 ] ; then
            echo "CMake could not be downloaded from Maxscale build server, trying from cmake.org"
            wget -q https://cmake.org/files/v3.16/${cmake_filename} --no-check-certificate
        fi
        sudo tar xzf ${cmake_filename} -C /usr/ --strip-components=1
    fi

  cmake_version=`${cmake_vrs_cmd} | grep "cmake version" | awk '{ print $3 }'`
  if verlt $cmake_version $cmake_version_required ; then
    echo "CMake installation failed"
    exit 1
  fi
fi

# TCL
system_tcl_version=$(tclsh <<< 'puts [info patchlevel]')
if verlt "$system_tcl_version" "8.6.5"
then
   mkdir tcl
   cd tcl
   wget -q --no-check-certificate http://prdownloads.sourceforge.net/tcl/tcl8.6.5-src.tar.gz

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

# Jansson
git clone https://github.com/akheron/jansson.git
if [ $? != 0 ]
then
    echo "Error cloning jansson"
    sudo rm -rf $tmpdir
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
wget -q -r -l1 -nH --cut-dirs=2 --no-parent -A.tar.gz --no-directories https://downloads.apache.org/avro/stable/c/
if [ $? != 0 ]
then
    echo "Error getting avro-c"
    sudo rm -rf $tmpdir
    exit 1
fi
avro_filename=`ls -1 avro*.tar.gz`
avro_dir=`echo "$avro_filename" | sed "s/.tar.gz//"`
tar -axf $avro_filename
mkdir $avro_dir/build
pushd $avro_dir/build
# Make sure the library isn't linked against snappy
sed -i 's/find_package(Snappy)//' ../lang/c/CMakeLists.txt
cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_C_FLAGS=-fPIC -DCMAKE_CXX_FLAGS=-fPIC
make
sudo make install
popd


if is_arm
then
    node_arch=arm64
else
    node_arch=x64
fi

wget --quiet https://nodejs.org/dist/v10.20.0/node-v10.20.0-linux-${node_arch}.tar.gz
tar -axf node-v10.20.0-linux-${node_arch}.tar.gz
sudo cp -t /usr -r node-v10.20.0-linux-${node_arch}/*

sudo rm -rf $tmpdir
