# Upgrading MariaDB MaxScale from 2.2 to 2.3

This document describes possible issues when upgrading MariaDB
MaxScale from version 2.2 to 2.3.

For more information about MariaDB MaxScale 2.3, please refer
to the [ChangeLog](../Changelog.md).

Before starting the upgrade, we recommend you back up your current
configuration file.

### `passwd` is no longer accepted

In the configuration file, passwords for monitors and services must be
specified using `password`; the support for the earlier deprecated
`passwd` has been removed. That is, the following
```
[The-Service]
type=service
passwd=some-service-password
...

[The-Monitor]
type=monitor
passwd=some-monitor-password
...
```
must be changed to
```
[The-Service]
type=service
password=some-service-password
...

[The-Monitor]
type=monitor
password=some-monitor-password
...
```
