# MariaDB MaxScale 2.4.15 Release Notes

Release 2.4.15 is a GA release.

This document describes the changes in release 2.4.15, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-2.4.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3360](https://jira.mariadb.org/browse/MXS-3360) MaxCtrl option --authenticator-options doesn't work
* [MXS-3337](https://jira.mariadb.org/browse/MXS-3337) galeramon queries only status, not variables
* [MXS-3326](https://jira.mariadb.org/browse/MXS-3326) Host class does not accept all valid domain names.
* [MXS-3325](https://jira.mariadb.org/browse/MXS-3325) Redis cache storage does not accept dashes in server names.
* [MXS-3324](https://jira.mariadb.org/browse/MXS-3324) Internal connections should explicitly set the autocommit state.
* [MXS-3318](https://jira.mariadb.org/browse/MXS-3318) Parsing error with comment
* [MXS-3314](https://jira.mariadb.org/browse/MXS-3314) Lack of prepared statement support for causal_reads is not documented
* [MXS-3158](https://jira.mariadb.org/browse/MXS-3158) Failover/switchover modifies event character set and collation

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
