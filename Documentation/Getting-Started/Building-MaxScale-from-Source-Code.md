# Building MariaDB MaxScale from Source Code

MariaDB MaxScale can be built on any system that meets the requirements. The main
requirements are as follows:

* CMake version 2.8 or later (Packaging requires version 2.8.12 or later)
* GCC version 4.4.7 or later
* SQLite3 version 3.3 or later
* OpenSSL
* Bison 2.7 or later
* Flex 2.5.35 or later
* libuuid
* GNUTLS

## Quickstart

This installs MaxScale as if it was installed from a package.

### Install dependencies

CentOS 7:

```
sudo yum install git gcc gcc-c++ ncurses-devel bison flex glibc-devel cmake \
     libgcc perl make libtool openssl openssl-devel pcre-devel \
     tcl tcl-devel systemtap-sdt-devel libuuid libuuid-devel sqlite sqlite-devel \
     gnutls-devel libgcrypt-devel
```

Ubuntu 16.04:

```
sudo apt-get update
sudo apt-get install git build-essential libssl-dev ncurses-dev bison flex \
     cmake perl libtool libpcre3-dev tcl tcl-dev uuid \
     uuid-dev libsqlite3-dev gnutls-dev libgcrypt20-dev
```

### Build and Install MaxScale

```
git clone https://github.com/mariadb-corporation/MaxScale
mkdir build
cd build
cmake ../MaxScale -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_CDC=N -DBUILD_MAXCTRL=N -DBUILD_LUA=N
make
sudo make install
sudo ./postinst
```

## Required packages

###  Required packages on CentOS/RHEL systems

The following packages are required on CentOS/RHEL 7. Older releases may require
other packages in addition to these.

```
git gcc gcc-c++ ncurses-devel bison flex glibc-devel cmake libgcc perl make \
libtool openssl openssl-devel pcre-devel tcl tcl-devel \
systemtap-sdt-devel libuuid libuuid-devel sqlite sqlite-devel
gnutls-devel libgcrypt-devel
```

### Required packages on Ubuntu and Debian systems

The following packages are required on Ubuntu 16.04. Different releases may
require other packages in addition to these.

```
git build-essential libssl-dev ncurses-dev bison flex cmake perl libtool \
 libpcre3-dev tlc tcl-dev uuid uuid-dev sqlite3-dev
libgnutls30 libgcrypt20
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
cmake ../MaxScale -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_CDC=N -DBUILD_MAXCTRL=N -DBUILD_LUA=N
```

This will install MariaDB MaxScale into `/usr/` and build the tests. The tests and
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
|TARGET_COMPONENT|Which component to install, default is the 'core' package. Other targets are 'experimental', which installs experimental packages, 'devel' which installs development headers and 'all' which installs all components.|
|TARBALL|Build tar.gz packages, requires PACKAGE=Y|

**Note**: You can look into [defaults.cmake](../../cmake/defaults.cmake) for a
list of the CMake variables.

## Building MariaDB MaxScale

Once the configuration is complete, you can compile, test and install MariaDB MaxScale.

```
make
make test
sudo make install
```

Other useful targets for Make are `documentation`, which generates the Doxygen documentation,
and `uninstall` which uninstall MariaDB MaxScale binaries after an install.

**Note**: If you configure CMake multiple times, it's possible that you will run
  into problems when building MaxScale. Most of the time this manifests as a
  missing _pcre2.h_ header file. When this happens, delete everything in the
  build directory and run the CMake command again.

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
cmake ../MaxScale -DPACKAGE=Y
```

Next step is to build the package.

```
make
make package
```

This will create a RPM/DEB package.

To build a tarball, add `-DTARBALL=Y` to the cmake invokation. This will create
a _maxscale-x.y.z.tar.gz_ file where _x.y.z_ is the version number.

Some Debian and Ubuntu systems suffer from a bug where `make package` fails
with errors from dpkg-shlibdeps. This can be fixed by running `make` before
`make package` and adding the path to the libmaxscale-common.so library to
the LD_LIBRARY_PATH environment variable.

```
make
LD_LIBRARY_PATH=$PWD/server/core/ make package
```

## Installing optional components

MaxScale is split into multiple components. The main component is the core MaxScale
package which contains MaxScale and all the modules. This is the default component
that is build, installed and packaged. There exist two other components, the _experimental_
and the _devel_ components. The former contains all experimental modules which are
not considered as part of the core MaxScale package and they can be alpha or beta
quality modules. The latter of the optional components, _devel_, contains the
development files required for MaxScale module development.

The component which is build is controlled by the TARGET_COMPONENT CMake variable.
The default value for this is _core_ which builds the core MaxScale package.

To build other components, you will need to set value of the TARGET_COMPONENT
CMake variable to the component name you wish to install or package.

### Install experimental modules

To install the experimental modules, invoke CMake with
_-DTARGET_COMPONENT=experimental_:

```
cmake ../MaxScale -DTARGET_COMPONENT=experimental
make
make install
```

### Creating a monolithic package

To create a monolithic package with all the components, set the
value of _TARGET_COMPONENT_ to 'all', _PACKAGE_ to Y and build the package:

```
cmake ../MaxScale -DPACKAGE=Y -DTARGET_COMPONENT=all
make package
```
