# MariaDB MaxScale 7.0 Release Notes

Release 7.0 is a Beta release.

This document describes the changes in release 7, when compared to
release 6.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Changed Features

## Dropped Features

MariaDB-Monitor settings `ignore_external_masters`, `detect_replication_lag`
`detect_standalone_master`, `detect_stale_master` and `detect_stale_slave`
have been removed. The first two were ineffective, the latter three are
replaced by `master_conditions` and `slave_conditions`.

## Deprecated Features

## New Features

## Bug fixes

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the supported Linux distributions.

Packages can be downloaded [here](https://mariadb.com/downloads/#mariadb_platform-mariadb_maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is `maxscale-X.Y.Z`. Further, the default branch is always the latest GA version
of MaxScale.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
