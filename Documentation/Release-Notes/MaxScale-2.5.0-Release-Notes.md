# MariaDB MaxScale 2.5.0 Release Notes

Release 2.5.0 is a Beta release.

This document describes the changes in release 2.5.0, when compared to
release 2.4.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

## Dropped Features

### Configuration parameters

The following deprecated parameters have been removed.

* `non_blocking_polls`
* `poll_sleep`

## Deprecated Features

## New Features

### Cache Invalidation

The MaxScale cache is now capable of performing invalidation of cache
entries. See the
[documentation](Filters/Cache.md#invalidation)
for more information.

## Bug fixes

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
