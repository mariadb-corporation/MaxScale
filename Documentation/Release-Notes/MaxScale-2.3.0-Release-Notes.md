# MariaDB MaxScale 2.3.0 Release Notes

Release 2.2.0 is a Beta release.

This document describes the changes in release 2.3.0, when compared to
release 2.2.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

## Dropped Features

## New Features

### Runtime Configuration of the Cache
With the variable `@maxscale.cache.enabled` it is now possible for a
client to specify whether the cache should be used. Please see the
[Cache](../Filters/Cache.md) documentation for details.

## Bug fixes

[Here is a list of bugs fixed in MaxScale 2.3.0.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.3.0)

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the Linux distributions supported
by MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/resources/downloads).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is X.Y.Z. Further, *master* always refers to the latest released non-beta version.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
