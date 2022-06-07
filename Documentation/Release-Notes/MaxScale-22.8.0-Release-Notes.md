# MariaDB MaxScale 22.8 Release Notes

The versioning scheme for MaxScale releases has changed; the format of the
version will be `YY.MM.PATCH` where `YY` is the last two digits of the year and
`MM` is the month when the release was made. The `PATCH` is a number that is
incremented whenever a maintenance release is made.

According to the old scheme, this MaxScale release would have been called 7 and
the version number would have been 7.0.0.

Release 22.8.0 is a Beta release.

This document describes the changes in release 22.8, when compared to
release 6.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Changed Features

## Dropped Features

### MariaDB Monitor

MariaDB-Monitor settings `ignore_external_masters`, `detect_replication_lag`
`detect_standalone_master`, `detect_stale_master` and `detect_stale_slave`
have been removed. The first two were ineffective, the latter three are
replaced by `master_conditions` and `slave_conditions`.

### REST API

The `/v1/maxscale/tasks/` endpoint has been removed from the REST-API.

### Database Firewall Filter

The `dbfwfilter` that was deprecated in MaxScale 6 has been removed in
MaxScale 22.8.

## Deprecated Features

### `ssl_ca_cert`

The server parameter `ssl_ca_cert` has been renamed to `ssl_ca` and
`ssl_ca_cert` has been deprecated. `ssl_ca_cert` is now an alias for
`ssl_ca` and can still be used, but we suggest taking `ssl_ca` into
use, as the support for `ssl_ca_cert` will at some point be dropped.

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
