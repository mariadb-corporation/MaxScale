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

This is the minimum set of requirements that must be met to build the MaxScale
core package.

## Quickstart

This installs MaxScale as if it was installed from a package. Install `git` before running the following commands.

```
git clone https://github.com/mariadb-corporation/MaxScale
mkdir build
cd build
../MaxScale/BUILD/install_build_deps.sh
cmake ../MaxScale -DCMAKE_INSTALL_PREFIX=/usr
make
sudo make install
sudo ./postinst
```

## Required Packages

For a definitive list of packages, consult the
[install_build_deps.sh](../../BUILD/install_build_deps.sh) script.

## Configuring the Build

The tests and other parts of the build can be controlled via CMake arguments.

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

## `make test` and Other Useful Targets

To run the MaxScale unit test suite, configure the build with `-DBUILD_TESTS=Y`,
compile and then run the `make test` command.

Other useful targets for Make are `documentation`, which generates the Doxygen documentation,
and `uninstall` which uninstall MariaDB MaxScale binaries after an install.

**Note**: If you configure CMake multiple times, it's possible that you will run
  into problems when building MaxScale. Most of the time this manifests as a
  missing _pcre2.h_ header file. When this happens, delete everything in the
  build directory and run the CMake command again.

# Building MariaDB MaxScale packages

If you wish to build packages, just add `-DPACKAGE=Y` to the CMake invocation
and build the package with `make package` instead of installing MaxScale with
`make install`. This process will create a RPM/DEB package depending on your
system.

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
