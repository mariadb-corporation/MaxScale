# MariaDB MaxScale 2.4.18 Release Notes

Release 2.4.18 is a GA release.

This document describes the changes in release 2.4.18, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-2.4.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3800](https://jira.mariadb.org/browse/MXS-3800) Not enough information in server state change messages
* [MXS-3609](https://jira.mariadb.org/browse/MXS-3609) Some statistics use 32-bit integers
* [MXS-3582](https://jira.mariadb.org/browse/MXS-3582) [readwritesplit] Failed to execute session command
* [MXS-3538](https://jira.mariadb.org/browse/MXS-3538) Removal of authenticator_options is not documented in upgrade documents
* [MXS-3535](https://jira.mariadb.org/browse/MXS-3535) user variable is not collected if it's in join clause
* [MXS-3533](https://jira.mariadb.org/browse/MXS-3533) MaxScale doesn't advertise the SESSION_TRACK capability
* [MXS-3529](https://jira.mariadb.org/browse/MXS-3529) Maxscale is not compatible with the latest cmake 3.20
* [MXS-3487](https://jira.mariadb.org/browse/MXS-3487) Old master connection is left open after transaction migration
* [MXS-3415](https://jira.mariadb.org/browse/MXS-3415) --export-config uses default file permissions
* [MXS-3114](https://jira.mariadb.org/browse/MXS-3114) Listener creation via REST API with sockets doesn't work

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
