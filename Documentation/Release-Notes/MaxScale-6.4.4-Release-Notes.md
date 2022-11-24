# MariaDB MaxScale 6.4.4 Release Notes

Release 6.4.4 is a GA release.

This document describes the changes in release 6.4.4, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-6.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-4404](https://jira.mariadb.org/browse/MXS-4404) Maxscale: KafkaCDC writes to current_gtid.txt causes high disk utilisation.
* [MXS-4397](https://jira.mariadb.org/browse/MXS-4397) fields parameter breaks REST-API filtering
* [MXS-4393](https://jira.mariadb.org/browse/MXS-4393) Authentication failures during shard mapping are not handled correctly
* [MXS-4389](https://jira.mariadb.org/browse/MXS-4389) Crash in handleError
* [MXS-4388](https://jira.mariadb.org/browse/MXS-4388) LOAD DATA LOCAL INFILE and changing autocomit causing stuck
* [MXS-4372](https://jira.mariadb.org/browse/MXS-4372) MAXGUI  - Out of memory in client PC browser.
* [MXS-4353](https://jira.mariadb.org/browse/MXS-4353) /maxscale/logs/data endpoint doesn't filter syslog contents correctly
* [MXS-4348](https://jira.mariadb.org/browse/MXS-4348) Full SASL support is not enabled for kafka modules
* [MXS-4317](https://jira.mariadb.org/browse/MXS-4317) Smartrouter interrupts the wrong query
* [MXS-3043](https://jira.mariadb.org/browse/MXS-3043) Database grants in user_accounts_file should add the database to the list of known databases

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
