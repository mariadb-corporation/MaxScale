# MariaDB MaxScale 23.02.1 Release Notes -- 2023-03-20

Release 23.02.1 is a GA release.

This document describes the changes in release 23.02.1, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-23.02.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-4302](https://jira.mariadb.org/browse/MXS-4302) Implement a way to specify a list of users or exclude a list of users
* [MXS-3004](https://jira.mariadb.org/browse/MXS-3004) Add semi-sync support to Pinloki

## Bug fixes

* [MXS-4555](https://jira.mariadb.org/browse/MXS-4555) Dynamic filter capabilities do not work
* [MXS-4552](https://jira.mariadb.org/browse/MXS-4552) "Unknown prepared statement handler" error when connection_keepalive is disabled on a readconnroute service
* [MXS-4547](https://jira.mariadb.org/browse/MXS-4547) Empty regex // is not treated as empty
* [MXS-4540](https://jira.mariadb.org/browse/MXS-4540) transaction replay retries repeatedly after failing checksum
* [MXS-4410](https://jira.mariadb.org/browse/MXS-4410) QLA filter not properly logging USE DBx command.

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
