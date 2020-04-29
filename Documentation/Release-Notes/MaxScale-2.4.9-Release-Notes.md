# MariaDB MaxScale 2.4.9 Release Notes

Release 2.4.9 is a GA release.

This document describes the changes in release 2.4.9, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-2861](https://jira.mariadb.org/browse/MXS-2861) Allow TLS cipher selection between Maxscale and client

## Bug fixes

* [MXS-2972](https://jira.mariadb.org/browse/MXS-2972) USE sent to wrong server
* [MXS-2969](https://jira.mariadb.org/browse/MXS-2969) maxscale service still not stopped / restarted on package upgrade
* [MXS-2968](https://jira.mariadb.org/browse/MXS-2968) Avrorouter direct replication sets wrong server_id
* [MXS-2956](https://jira.mariadb.org/browse/MXS-2956) admin_ssl_ca_cert reads the wrong certificate
* [MXS-2954](https://jira.mariadb.org/browse/MXS-2954) cluster sync doesn't update global configuration options
* [MXS-2948](https://jira.mariadb.org/browse/MXS-2948) Fix cluster sync
* [MXS-2943](https://jira.mariadb.org/browse/MXS-2943) csmon doesn't work with pluggable ColumnStore
* [MXS-2942](https://jira.mariadb.org/browse/MXS-2942) DELETE of a monitor doesn't check whether it uses servers
* [MXS-2939](https://jira.mariadb.org/browse/MXS-2939) Session commands do not trigger reconnection
* [MXS-2931](https://jira.mariadb.org/browse/MXS-2931) MXS-2810 still unfixed in RHEL / CentOS 6 packages

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for supported the Linux distributions.

Packages can be downloaded [here](https://mariadb.com/downloads/#mariadb_platform-mariadb_maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is `maxscale-X.Y.Z`. Further, the default branch is always the latest GA version
of MaxScale.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
