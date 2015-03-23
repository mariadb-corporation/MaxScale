# Upgrading MaxScale from 1.0 to 1.1

This document describes upgrading MaxScale from version 1.0.5 to 1.1.0 and the major differences in the new version compared to the old version. The major changes can be found in the `Changelog.txt` file in the installation directory and the official release notes in the `ReleaseNotes.txt` file.

## Installation

If you are installing MaxScale from a RPM package, we recommend you back up your configuration and log files and that you remove the old installation of MaxScale completely. If you choose to upgrade MaxScale instead of removing it and re-installing it afterwards, the init scripts in `/etc/init.d` folder will be missing. This is due to the RPM packaging system but the script can be re-installed by running the `postinst` script found in the `/usr/local/mariadb-maxscale` folder.

```
# Re-install init scripts
cd /usr/local/mariadb-maxscale
./postinst
```

The 1.1.0 version of MaxScale installs into `/usr/local/mariadb-maxscale` instead of `/usr/local/skysql/maxscale`. This will cause external references to MaxScale's home directory to stop working so remember to update all paths with the new version.

## MaxAdmin changes

The MaxAdmin client's default password in MaxScale 1.1.0 is `mariadb` instead of `skysql`.
