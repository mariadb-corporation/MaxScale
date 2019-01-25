# Upgrading MariaDB MaxScale from 2.3 to 2.4

This document describes possible issues when upgrading MariaDB
MaxScale from version 2.3 to 2.4.

For more information about MariaDB MaxScale 2.4, please refer
to the [ChangeLog](../Changelog.md).

Before starting the upgrade, we recommend you back up your current
configuration file.

## Section Names

Section and object names starting with `@@` are now reserved for
internal use by MaxScale.

In case such names have been used, they must manually be changed
in all configuration files of MaxScale, before MaxScale 2.4 is started.

Those files are:

* The main configuration file; typically `/etc/maxscale.cnf`.
* All nested configuration files; typically `/etc/maxscale.cnf.d/*`.
* All dynamic configuration files; typically `/var/lib/maxscale/maxscale.cnd.d/*`.

Further, whitespace in section names that was deprecated in MaxScale 2.2
will now be rejected, which will cause the startup of MaxScale to fail.

To prevent that, section names like
```
[My Server]
...

[My Service]
...
servers=My Server
```
must be changed, for instance, to
```
[MyServer]
...

[MyService]
...
servers=MyServer
```
