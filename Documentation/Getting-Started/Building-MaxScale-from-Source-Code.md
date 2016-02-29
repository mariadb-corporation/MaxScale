# Building MaxScale from Source Code

You will need a number of tools and libraries in order to achieve this.

* cmake version 2.8.12 or later

* gcc recommended version 4.4.7 or later (MariaDB 10 libraries require gcc 4.7 or newer)

* libaio

* MariaDB Develop libraries version 5.5.38 or later

* libedit 2.11 or later (used by the MaxAdmin tool)

* Bison and Flex

# Obtaining MariaDB packages

MaxScale requires the server and the development packages for the MariaDB server. Either the 10.0 or the 5.5 version of the MariaDB server can be used. You can obtain these by following the instructions on the MariaDB.org site: [https://downloads.mariadb.org/](https://downloads.mariadb.org/)

After following the instructions on that site you should have a working MariaDB repository if you use Yum or APT or the binary packages if you prefer a manual download.

# Build dependencies

The full list of dependencies for the most common distributions is provided in this section. If your system is not listed here, MaxScale building isn't guaranteed to be compatible but might still be successful.

## RHEL and CentOS

You will need to install all of the following packages for all versions of RHEL and CentOS.

```
gcc gcc-c++ ncurses-devel bison flex glibc-devel cmake libgcc perl make libtool \
    openssl-devel libaio libaio-devel librabbitmq-devel libcurl-devel pcre-devel
```

In addition, if you wish to to build an RPM package include:

```
rpm-build
```

There are also some version specific packages you need to install.

#### RHEL 6, 7, CentOS 6, 7:

```
libedit-devel
```

#### RHEL 7, CentOS 7:

```
mariadb-devel mariadb-embedded-devel 
```

#### RHEL 5, 6, CentOS 5, 6
```
MariaDB-devel MariaDB-server
```

## Ubuntu and Debian

These packages are required on all versions of Ubuntu and Debian.

```
	build-essential libssl-dev libaio-dev ncurses-dev bison flex \
	cmake perl libtool librabbitmq-dev libcurl-dev libpcre3-dev
```

If you want to build a DEB package, you will also need:

```
	dpkg-dev
```

You will also need some version specific packages.

#### Ubuntu 14.04 or later, Debian 8 (Jessie) or later

*At the time of writing, the libmariadbd-dev package is broken and does not contain the required libmysqld.a library. Please follow the install instructions for earlier version of Ubuntu and Debian.*

```
	libmariadbclient-dev libmariadbd-dev                            
```

#### Earlier versions of Ubuntu or Debian

For these, you will need to obtain the MariaDB embedded library. It has to be manually extracted from the tarballs at the MariaDB site. But first ascertain what version of glibc is installed. Run the command:

```
	dpkg -l | grep libc6
```

which will show the version number. For versions older than 2.14 you should obtain the library which supports GLIBC versions older than 2.14 and for newer versions, the library which supports newer GLIBC versions should be used.

The suggested location for extracting the tarball is `/usr` so the operation can be done by the following commands:

```
	cd /usr
	tar -xzvf /path/to/mariadb.library.tar.gz
```

where /path/to/mariadb.library.tar.gz is replaced by the actual path and name of the downloaded tarball.

# Obtaining the MaxScale Source Code

Now clone the GitHub project to your machine either via the web interface, your favorite graphical interface or the git command line

```
$ git clone https://github.com/mariadb-corporation/MaxScale
Cloning into 'MaxScale'...
remote: Counting objects: 16228, done.
...
```

Change directory to the `MaxScale` directory, create a build directory and change directory to that build directory

```
$ cd MaxScale  
$ mkdir build
$ cd build
```

The next step is to run the `cmake` command to build the Makefile you need to compile Maxscale. There are a number of options you may give to configure cmake and point it to the various packages it requires. In this example we will assume the MariaDB developer packages have been installed as described above and set all the options required to locate these, along with options to build the unit tests and configure the installation target directory.

If you run into any trouble while configuring CMake, you can always remove the 
`CMakeCache.txt` file to clear CMake's internal cache. This resets all values to their 
defaults and can be used quickly force a reconfiguration of CMake variables. There is also a make target, `make rebuild_cache`, that cleans the CMake cache.
This is also a good reason why you should always build into a separate directory, because you can safely 
wipe the build directory clean without the danger of deleting important files when 
something goes wrong. Building 'out-of-source' also allows you to have multiple 
configurations of MaxScale at the same time.

The default values that MaxScale uses for CMake can be found in the 'macros.cmake' file under the `cmake` folder. 
If you wish to change these, edit the 'macros.cmake' file or define the variables manually at configuration time.

To display all CMake variables with their descriptions:

```
cmake .. -LH
```
This is a useful command if you have your libraries installed in non-standard locations and need to provide them manually.

When you are ready to run cmake, provide the following command:

```
cmake ..
```
This will automatically search your system for the right files and libraries and if you have your libraries installed in standard locations, it should succeed. If there are errors with the CMake configuration, read the error messages, provide the needed variables for CMake and call `cmake` again with the additional parameters.

Here is an example of a cmake call with parameters for custom library locations, building of tests and without the installation of init scripts or the example maxscale.cnf file.

```
$ cmake .. -DMYSQL_DIR=/usr/mariadb-5.5.41-linux-x86_64/include/mysql \
-DEMBEDDED_LIB=/usr/mariadb-5.5.41-linux-x86_64/lib/libmysqld.a \
-DMYSQLCLIENT_LIBRARIES=/usr/mariadb-5.5.41-linux-x86_64/lib/libmysqlclient.so \
-DERRMSG=/usr/mariadb-5.5.41-linux-x86_64/share/english/errmsg.sys \
-DCMAKE_INSTALL_PREFIX=/home/maxscale/MaxScale -DBUILD_TESTS=Y \
-DWITH_SCRIPTS=N -DWITH_MAXSCALE_CNF=N

<pre>
-- CMake version: 2.8.12.2
-- The C compiler identification is GNU 4.4.7
-- The CXX compiler identification is GNU 4.4.7
-- Check for working C compiler: /usr/bin/cc
-- Check for working C compiler: /usr/bin/cc -- works
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working CXX compiler: /usr/bin/c++
-- Check for working CXX compiler: /usr/bin/c++ -- works
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Library was found at: /lib64/libaio.so
-- Library was found at: /usr/lib64/libssl.so
-- Library was found at: /usr/lib64/libcrypt.so
-- Library was found at: /usr/lib64/libcrypto.so
-- Library was found at: /usr/lib64/libz.so
-- Library was found at: /usr/lib64/libm.so
-- Library was found at: /usr/lib64/libdl.so
-- Library was found at: /usr/lib64/librt.so
-- Library was found at: /usr/lib64/libpthread.so
-- Using errmsg.sys found at: /home/maxscale/usr/share/mysql/english/errmsg.sys
-- Using embedded library: /home/mpinto/usr/lib64/libmysqld.a
-- Valgrind found: /usr/bin/valgrind
-- Found dynamic MySQL client library: /home/maxscale/usr/lib64/libmysqlclient.so
-- Found static MySQL client library: /usr/lib/libmysqlclient.a
-- C Compiler supports: -Werror=format-security
-- Linking against: /home/mpinto/usr/lib64/libmysqlclient.so
-- Installing MaxScale to: /usr/local/maxscale/
-- Generating RPM packages
-- Found Doxygen: /usr/bin/doxygen (found version "1.6.1") 
-- Configuring done
-- Generating done
-- Build files have been written to: /home/maxscale/develop/build
```

Once the cmake command is complete simply run make to build the MaxScale binaries.

```
$ make

<pre>
**Scanning dependencies of target utils**
[  1%] Building CXX object utils/CMakeFiles/utils.dir/skygw_utils.cc.o
**Linking CXX static library libutils.a**
[  1%] Built target utils
**Scanning dependencies of target log_manager**
[  2%] Building CXX object log_manager/CMakeFiles/log_manager.dir/log_manager.cc.o
...
</pre>
```

After the completion of the make process the installation can be achieved by running the make install target.

```
$ make install
...
```

This will result in an installation being created which is identical to that which would be achieved by installing the binary package.

When building from source, MaxScale installs to `/usr/local/` and places init.d scripts and ldconfig files into their folders. Change the `CMAKE_INSTALL_PREFIX` variable to your desired installation directory and set `WITH_SCRIPTS=N` to prevent the init.d script and ldconfig file installation.

Other useful targets for Make are `documentation`, which generates the Doxygen documentation, and `uninstall` which uninstall MaxScale binaries after an install.

## Running the MaxScale testsuite

MaxScale has a core test suite for internal components and an extended suite of test for modules. To run the core tests, run `make testcore`. This will test the core maxscale executable.

To run `make testall`, the full test suite, you need to have four mysqld servers running on localhost. It assumes a master-slave replication setup with one master and three slaves.

The ports to which these servers are listening and the credentials to use for testing can be specified in the `macros.cmake` file found in the root source folder.

On the master full privileges on the databases `test` are needed, on the slaves `SELECT` permissions on `test.*` should be sufficient.

When you run the `make testall` target after configuring the build with CMake a local version of MaxScale is installed into the build folder. After this a MaxScale instance is started and the test set is executed. 

After testing has finished you can find a full testlog generated by CTest in `Testing/Temporary/` directory and MaxScale's log files in the `log/` directory of the build root.

## Building the MaxScale package

First make sure you have the required libraries for your platform, including either rpmbuild for RHEL variants or dpkg-dev for Debian variants.

If you wish to generate your own MaxScale package, you can do so by first configuring CMake with -DPACKAGE=Y. This will enable the package building target, `package` for the Makefile build system. After configuring, it should be as simple as running the `make package` command in the build directory. This will result in two packages, a tar.gz package and either a .rpm package or a .deb package depending on your system.
