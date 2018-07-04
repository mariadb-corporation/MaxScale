# MariaDB MaxScale 2.2.11 Release Notes -- 2018-06

Release 2.2.11 is a GA release.

This document describes the changes in release 2.2.11, when compared to
release 2.2.10.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

### Cache

The cache filter is no longer marked as being experimental. The default
value for `cached_data` been changed from `shared` to `thread_specific`,
and the default value for `selects` has been changed from `verify_cacheable`
to `assume_cacheable`.
Please consult the
[cache documentation](../Filters/Cache.md)
for details.

## Bug fixes

* [MXS-1953](https://jira.mariadb.org/browse/MXS-1953) MaxScale hangs
* [MXS-1950](https://jira.mariadb.org/browse/MXS-1950) No explanation for routing failure when no valid servers are available
* [MXS-1948](https://jira.mariadb.org/browse/MXS-1948) Connections not balanced between workers

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the Linux distributions supported
by MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/downloads/mariadb-tx/maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is X.Y.Z. Further, *master* always refers to the latest released non-beta version.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
