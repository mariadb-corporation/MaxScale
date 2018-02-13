# Upgrading MariaDB MaxScale from 2.1 to 2.2

This document describes possible issues upgrading MariaDB MaxScale from version
2.1 to 2.2.

For more information about MariaDB MaxScale 2.2, please refer to the
[ChangeLog](../Changelog.md).

Before starting the upgrade, we recommend you back up your current configuration
file.

### Administrative Users

The file format for the administrative users used by MaxScale has been
changed. Old style files are automatically upgraded and a backup of the old file is
stored in `/var/lib/maxscale/passwd.backup`.

### Regular Expression Parameters

Modules may now use a built-in regular expression string parameter type instead
of a normal string when accepting patterns. The modules that use the new regex
parameter type are *qlafilter* and *tee*. When inputting pattern, enclose the
string in slashes, e.g. `match=/^select/` defines the pattern `^select`.

### Binlog Server

Binlog server automatically accepts GTID connection from MariaDB 10 slave servers
by saving all incoming GTIDs into a SQLite map database.

### MaxCtrl Included in Main Package

In the 2.2.1 beta version MaxCtrl was in its own package whereas in 2.2.2
it is in the main `maxscale` package. If you have a previous installation
of MaxCtrl, please remove it before upgrading to MaxScale 2.2.2.
