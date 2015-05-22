# MaxScale Release Notes

## 1.2 GA

This document details the changes in version 1.2 since the release of the 1.1.1 GA Release of the MaxScale product.

###***PLEASE NOTICE: MaxScale installation directories have changed in this version***
The 1.2 version of MaxScale differs from previos versions in its installation layout. Please take great care when upgrading MaxScale from previous versions to version 1.2. An automatic upgrade will not work due to the severe changes in the installation layout.

## New Features

### Non-root MaxScale
You can now run MaxScale as any user. The standard installation of a MaxScale package now creates the maxscale user and the maxscale group.

### FHS-compliant installation
The 1.2 version of MaxScale now complies to the Filesystem Hierarchy Standard. This means that MAXSCALE_HOME is no longer necessary and directories can be moved to different locations.

A quick list of changes in installation directories and file names:

  * Binaries go into `/usr/bin`
  * Configuration files to `/etc` and the configuration file is now lower case: `maxscale.cnf`
  * Logs to `/var/log/maxscale`
  * The module and library directory have been combined into a single directory in `/usr/lib64/maxscale`. If you have custom modules please make sure they are located there.
  * Data directory is `/var/lib/maxscale`. This is the default location for MaxScale-specific data.
  * PID file can be found at `/var/run/maxscale`

