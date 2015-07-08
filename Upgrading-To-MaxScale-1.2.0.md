# Upgrading MaxScale from 1.1 to 1.2

This document describes upgrading MaxScale from version 1.1.1 to 1.2 and the major differences in the new version compared to the old version. The major changes can be found in the `Changelog.txt` file in the installation directory and the official release notes in the `ReleaseNotes.txt` file.

## Installation

Before starting the upgrade, we recommend you back up your configuration, log and binary log files in `/usr/local/mariadb-maxscale/`.

Upgrading MaxScale will copy the `MaxScale.cnf` file in `/usr/local/mariadb-maxscale/etc/` to `/etc/` and renamed to `maxscale.cnf`. Binary log files are not automatically copied and should be manually moved from `/usr/local/mariadb-maxscale` to `/var/lib/maxscale/`.

## File location changes

MaxScale 1.2 follows the [FHS-standard](http://www.pathname.com/fhs/) and installs to `/usr/` and `/var/` subfolders. Here are the major changes and file locations.

* Configuration files are located in `/etc/` and use lowercase letters: `/etc/maxscale.cnf`
* Binary files are in `/usr/bin/`
* Libraries and modules are in `/usr/lib64/maxscale/`. If you are using custom modules, please make sure they are in this directory before starting MaxScale.
* Log files are in the `var/log/maxscale/` folder
* MaxScale's PID file is located in `/var/run/maxscale/maxscale.pid`
* Data files and other persistent files are in `/var/lib/maxscale/`

## Running MaxScale without root permissions

MaxScale can run as a non-root user with the 1.2 version. RPM and DEB packages install the `maxscale` user and `maxscale` group which are used by the init scripts and systemd configuration files. If you are installing from a binary tarball, you can run the `postinst` script included in it to manually create these groups.
