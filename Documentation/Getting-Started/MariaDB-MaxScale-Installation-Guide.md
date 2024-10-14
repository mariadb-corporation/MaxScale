# MariaDB MaxScale Installation Guide

## Install MariaDB MaxScale From MariaDB Repositories

The recommended approach is to use [the MariaDB package
repository](https://mariadb.com/kb/en/library/mariadb-package-repository-setup-and-usage/)
to install MaxScale. After enabling the repository by following the
instructions, MaxScale can be installed with the following commands.

* For RHEL/Rocky Linux/Alma Linux, use `dnf install maxscale`.

* For Debian and Ubuntu, run `apt update` followed by `apt install maxscale`.

* For SLES, use `zypper install maxscale`.

## Install MariaDB MaxScale From a RPM/DEB Package

Download the correct MaxScale package for your CPU architecture and operating
system from [the MariaDB Downloads
page](https://mariadb.com/downloads/community/maxscale/). MaxScale can be
installed with the following commands.

* For RHEL/Rocky Linux/Alma Linux, use `dnf install /path/to/maxscale-*.rpm`

* For Debian and Ubuntu, use `apt install /path/to/maxscale-*.deb`.

* For SLES, use `zypper install /path/to/maxscale-*.rpm`.

## Install MariaDB MaxScale Using a Tarball

MaxScale can also be installed using a tarball.
That may be required if you are using a Linux distribution for which there
exist no installation package or if you want to install many different
MaxScale versions side by side. For instructions on how to do that, please refer to
[Install MariaDB MaxScale using a Tarball](Install-MariaDB-MaxScale-Using-a-Tarball.md).

## Building MariaDB MaxScale From Source Code

Alternatively you may download the MariaDB MaxScale source and build your own binaries.
To do this, refer to the separate document
[Building MariaDB MaxScale from Source Code](Building-MaxScale-from-Source-Code.md)

## Assumptions

### Memory allocation behavior

MaxScale assumes that memory allocations always succeed and in general does
not check for memory allocation failures. This assumption is compatible with
the Linux kernel parameter
[`vm.overcommit_memory`](https://www.kernel.org/doc/Documentation/vm/overcommit-accounting)
having the value `0`, which is also the default on most systems.

With `vm.overcommit_memory` being `0`, memory _allocations_ made by an
application never fail, but instead the application may be killed by the
so-called OOM (out-of-memory) killer if, by the time the application
actually attempts to _use_ the allocated memory, there is not available
free memory on the system.

If the value is `2`, then a memory allocation made by an application may
fail and unless the application is prepared for that possiblity, it will
likely crash with a SIGSEGV. As MaxScale is not prepared to handle memory
allocation failures, it will crash in this situation.

The current value of `vm.overcommit_memory` can be checked with
```
sysctl vm.overcommit_memory
```
or
```
cat /proc/sys/vm/overcommit_memory
```

## Configuring MariaDB MaxScale

[The MaxScale Tutorial](../Tutorials/MaxScale-Tutorial.md) covers the first
steps in configuring your MariaDB MaxScale installation. Follow this tutorial
to learn how to configure and start using MaxScale.

For a detailed list of all configuration parameters, refer to the
[Configuration Guide](Configuration-Guide.md) and the module specific documents
listed in the [Documentation Contents](../Documentation-Contents.md#routers).

## Encrypting Passwords

Read the [Encrypting Passwords](Configuration-Guide.md#encrypting-passwords)
section of the configuration guide to set up password encryption for the
configuration file.

## Administration Of MariaDB MaxScale

There are various administration tasks that may be done with MariaDB MaxScale.
A command line tools is available, [maxctrl](../Reference/MaxCtrl.md), that will
interact with a running MariaDB MaxScale and allow the status of MariaDB
MaxScale to be monitored and give some control of the MariaDB MaxScale
functionality.

[The administration tutorial](../Tutorials/Administration-Tutorial.md)
covers the common administration tasks that need to be done with MariaDB MaxScale.
