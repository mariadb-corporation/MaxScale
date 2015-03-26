# Building MaxScale from Source Code

You will need a number of tools and libraries in order to achieve this.

* cmake version 2.8.12 or later

* gcc recommended version 4.4.7 or later

* libaio

* MariaDB Develop libraries version 5.5.38 or later

* libedit 2.11 or later (used by the MaxAdmin tool)

## Build dependencied

The full list of dependencies for the most common distributions is provided in this section. If your system is not listed here, MaxScale building isn't guaranteed to be compatible but might still be successful.

### All RHEL, CentOS and Fedora versions

```
gcc gcc-c++ ncurses-devel bison glibc-devel cmake libgcc perl make libtool 
openssl-devel libaio libaio-devel librabbitmq-devel
```

In addition, if you wish to to build an RPM package include:

```
	rpm-build                           
```

#### RHEL 6, 7, CentOS 6, 7, Fedora:

```
libedit-devel
```

#### RHEL 7, CentOS 7:

```
mariadb-devel mariadb-embedded-devel 
```

#### RHEL 5, 7, CentOS 5, 6, Fedora 19, 20
MariaDB-devel MariaDB-server

#### Fedora 19, 20

```
systemtap-sdt-devel
```

### All Ubuntu and Debian versions

```
	build-essential libssl-dev libaio-dev ncurses-dev bison
	cmake perl libtool librabbitmq-dev     
```

If you want to build a DEB package, you will also need:

```
	dpkg-dev
```

#### Ubuntu 14.04 or later, Debian 8 (Jessie) or later

```
	libmariadbclient-dev libmariadbd-dev                            
```

#### Earlier versions of Ubuntu or Debian

For these, you will need to obtain the MariaDB embedded library. It has to be manually extracted from the tarball. But first ascertain what version of glibc is installed. Run the command:

```
	dpkg -l | grep libc6
```

which will show the version number. If the version is less than 2.14 you should obtain the library from:
[https://downloads.mariadb.org/interstitial/mariadb-5.5.41/bintar-linux-x86_64/mariadb-5.5.41-linux-x86_64.tar.gz](https://downloads.mariadb.org/interstitial/mariadb-5.5.41/bintar-linux-x86_64/mariadb-5.5.41-linux-x86_64.tar.gz). 
Otherwise, from:
[https://downloads.mariadb.org/interstitial/mariadb-5.5.41/bintar-linux-glibc_214-x86_64/mariadb-5.5.41-linux-glibc_214-x86_64.tar.gz](https://downloads.mariadb.org/interstitial/mariadb-5.5.41/bintar-linux-glibc_214-x86_64/mariadb-5.5.41-linux-glibc_214-x86_64.tar.gz)

The suggested location for extracting the tarball is `/usr` so the operation can be done by the following commands:

```
	cd /usr
	tar -xzvf /path/to/mariadb.library.tar.gz
```

where /path/to/mariadb.library.tar.gz is replaced by the actual path and name of the downloaded tarball.

### OpenSUSE

At the time this guide was written, the MariaDB development packages for OpenSUSE were broken and the build failed.

The packages required are:

```
gcc gcc-c++ ncurses-devel bison glibc-devel cmake libgcc_s1 perl 
make libtool libopenssl-devel libaio libaio-devel 
libedit-devel librabbitmq-devel
	MariaDB-devel MariaDB-client MariaDB-server
```

If zypper ask which MariaDB client should be installed `MariaDB-client` or `mariadb-client`
	 please select `MariaDB-client`. This is the package provided by the MariaDB repository.

##Obtaining the MaxScale Source Code

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

The next step is to run the cmake command to build the Makefile you need to compile Maxscale. There are a number of options you may give to configure cmake and point it to the various packages it requires. In this example we will assume the MariaDB developer packages have been installed as described above and set all the options required to locate these, along with options to build the unit tests and configure the installation target directory.

If you run into any trouble while configuring CMake, you can always remove the 
`CMakeCache.txt` file to clear CMake's internal cache. This resets all values to their 
defaults and can be used quickly force a reconfiguration of CMake variables. There is also a make target, `make rebuild_cache`, that cleans the CMake cache.
This is also a good reason why you should always build into a separate directory, because you can safely 
wipe the build directory clean without the danger of deleting important files when 
something goes wrong. Building 'out-of-source' also allows you to have multiple 
configurations of MaxScale at the same time.

The default values that CMake uses can be found in the 'macros.cmake' file. 
If you wish to change these, edit the 'macros.cmake' file or define the 
variables manually at configuration time.

To display all CMake variables with their descriptions:

```
cmake -LH <path to source>
```

When you are ready to run cmake:

```
$ cmake -DMYSQL_DIR=/usr/mariadb-5.5.41-linux-x86_64/include/mysql \
-DEMBEDDED_LIB=/usr/mariadb-5.5.41-linux-x86_64/lib/libmysqld.a \
-DMYSQLCLIENT_LIBRARIES=/usr/mariadb-5.5.41-linux-x86_64/lib/libmysqlclient.so \
-DERRMSG=/usr/mariadb-5.5.41-linux-x86_64/share/english/errmsg.sys \
-DCMAKE_INSTALL_PREFIX=/home/maxscale/MaxScale -DBUILD_TESTS=Y \
-DWITH_SCRIPTS=N

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

This will result in an installation being created which is identical to that which would be achieved by installing the binary package. The only difference is that init.d scripts aren't installed.

By default, MaxScale installs to `/usr/local/mariadb-maxscale` and places init.d scripts and ldconfig files into their folders. Change the `CMAKE_INSTALL_PREFIX` variable to your desired installation directory and set `WITH_SCRIPTS=N` to prevent the init.d script and ldconfig file installation.

Other useful targets for Make are `documentation`, which generates the Doxygen documentation, and `uninstall` which uninstall MaxScale binaries after an install.

## Running the MaxScale testsuite

MaxScale has a core test suite for internal components and an extended suite of test for modules. To run the core tests, run `make testcore`. This will test the core maxscale executable.

To run `make testall`, the full test suite, you need to have four mysqld servers running on localhost. It assumes a master-slave replication setup with one master and three slaves.

The ports to which these servers are listening and the credentials to use for testing can be specified in the `macros.cmake` file found in the root source folder.

On the master full privileges on the databases `test` are needed, on the slaves `SELECT` permissions on `test.*` should be sufficient.

When you run the `make testall` target after configuring the build with CMake a local version of MaxScale is installed into the build folder. After this a MaxScale instance is started and the test set is executed. 

After testing has finished you can find a full testlog generated by CTest in `Testing/Temporary/` directory and MaxScale's log files in the `log/` directory of the build root.

