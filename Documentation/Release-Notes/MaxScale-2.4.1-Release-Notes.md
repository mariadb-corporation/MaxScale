# MariaDB MaxScale 2.4.1 Release Notes

Release 2.4.1 is a Beta release.

This document describes the changes in release 2.4.1, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-2603](https://jira.mariadb.org/browse/MXS-2603) MaxScale causes connections to break in Percona PXC Cluster
* [MXS-2598](https://jira.mariadb.org/browse/MXS-2598) memory leak on handling COM_CHANGE_USER
* [MXS-2597](https://jira.mariadb.org/browse/MXS-2597) MaxScale doesn't handle errors from microhttpd
* [MXS-2594](https://jira.mariadb.org/browse/MXS-2594) Enabling use_priority for not set priority on server level triggers an election
* [MXS-2578](https://jira.mariadb.org/browse/MXS-2578) Maxscale RPM issue PCI Compliancy
* [MXS-2577](https://jira.mariadb.org/browse/MXS-2577) Avrorouter direct conversion is not documented
* [MXS-2574](https://jira.mariadb.org/browse/MXS-2574) maxctrl alter user doesn't work on current user
* [MXS-2544](https://jira.mariadb.org/browse/MXS-2544) PAMAuth doesn't check role permissions
* [MXS-2521](https://jira.mariadb.org/browse/MXS-2521) COM_STMT_EXECUTE maybe return empty result
* [MXS-2446](https://jira.mariadb.org/browse/MXS-2446) Fatal on Maxscale server on reimaging clustrix setup being monitored.

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
