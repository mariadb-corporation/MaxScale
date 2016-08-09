# Building MariaDB MaxScale from Source Code

MariaDB MaxScale can be built on any system that meets the requirements. The main
requirements are as follows:

* CMake version 2.8 or later (Packaging requires version 2.8.12 or later)
* GCC version 4.4.7 or later
* libaio
* OpenSSL
* Bison 2.7 or later
* Flex 2.5.35 or later
* libuuid

## Required packages

###  Required packages on CentOS/RHEL systems

The following packages are required on CentOS/RHEL 7. Older releases may require
other packages in addition to these.

```
git gcc gcc-c++ ncurses-devel bison flex glibc-devel cmake libgcc perl make libtool \
openssl-devel libaio libaio-devel libcurl-devel pcre-devel tcl tcl-devel systemtap-sdt-devel libuuid libuuid-devel
```

You can install the packages with the following commands.

```
sudo yum install git gcc gcc-c++ ncurses-devel bison flex glibc-devel cmake libgcc perl \
     make libtool openssl-devel libaio libaio-devel librabbitmq-devel \
     libcurl-devel pcre-devel tcl tcl-devel systemtap-sdt-devel libuuid libuuid-devel
```

### Required packages on Ubuntu and Debian systems

The following packages are required on Ubuntu 14.04. Different releases may require
other packages in addition to these.

```
git build-essential libssl-dev libaio-dev ncurses-dev bison flex \
cmake perl libtool libcurl4-openssl-dev libpcre3-dev tlc tcl-dev uuid uuid-dev
```

You can install the packages with the following command.

```
sudo apt-get install git build-essential libssl-dev libaio-dev ncurses-dev \
bison flex cmake perl libtool libcurl4-openssl-dev libpcre3-dev tcl tcl-dev uuid uuid-dev
```

## Preparing the MariaDB MaxScale build

Clone the MariaDB MaxScale repository from GitHub.

```
git clone https://github.com/mariadb-corporation/MaxScale
```

Create a separate build directory where you can safely build MariaDB MaxScale
without altering the source code. Change the working directory to the
directory we just created.

```
mkdir build
cd build
```

## Configuring the build

The next step is to configure MariaDB MaxScale. You only need to execute the following
command to configure the build.

```
cmake ../MaxScale -DBUILD_TESTS=Y
```

This will install MariaDB MaxScale into `/usr/local/` and build the tests. The tests and
other parts of the installation can be controlled via CMake arguments.

Here is a small table with the names of the most common parameters and what
they control. These should all be given as parameters to the -D switch in
_NAME_=_VALUE_ format (e.g. `-DBUILD_TESTS=Y`).

|Argument Name|Explanation|
|--------|-----------|
|CMAKE_INSTALL_PREFIX|Location where MariaDB MaxScale will be installed to. Set this to `/usr` if you want MariaDB MaxScale installed into the same place the packages are installed.|
|BUILD_TESTS|Build tests|
|WITH_SCRIPTS|Install systemd and init.d scripts|
|PACKAGE|Enable building of packages|

**Note**: You can look into [defaults.cmake](../../cmake/defaults.cmake) for a
list of the CMake variables.

## Building MariaDB MaxScale

Once the configuration is complete, you can compile, test and install MariaDB MaxScale.

```
make
make test
sudo make install
```

Other useful targets for Make are `documentation`, which generates the Doxygen documentation, and `uninstall` which uninstall MariaDB MaxScale binaries after an install.

# Building MariaDB MaxScale packages

In addition to the packages needed to build MariaDB MaxScale, you will need the
packaging libraries for your system and CMake version 2.8.12 or later.

### CentOS/RHEL systems

```
sudo yum install rpm-build
```

### Ubuntu and Debian systems

```
sudo apt-get install dpkg-dev
```

Next step is to clone the MariaDB MaxScale repository from GitHub. If you already
cloned it when building MariaDB MaxScale, this step can be skipped.

```
git clone https://github.com/mariadb-corporation/MaxScale
```

Create a packaging directory and change the working directory to the
directory we just created.

```
mkdir packaging
cd packaging
```

Configure the build, giving it the same arguments we gave in the previous
section with a few changes. The only new thing is the `-DPACKAGE=Y` argument
which allows us to build packages. The `-DCMAKE_INSTALL_PREFIX` was removed since
we aren't installing MariaDB MaxScale, only packaging it.

```
cmake ../MaxScale -DPACKAGE=Y -DBUILD_TESTS=Y
```

Next step is to test and build the package.

```
make
make test
make package
```

This will create a tarball and a RPM/DEB package.

Some Debian and Ubuntu systems suffer from a bug where `make package` fails
with errors from dpkg-shlibdeps. This can be fixed by running `make` before
`make package` and adding the path to the libmaxscale-common.so library to
the LD_LIBRARY_PATH environment variable.

```
make
LD_LIBRARY_PATH=$PWD/server/core/ make package
```
