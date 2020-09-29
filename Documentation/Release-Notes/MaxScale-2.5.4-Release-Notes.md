# MariaDB MaxScale 2.5.4 Release Notes -- 2020-09-29

Release 2.5.4 is a GA release.

This document describes the changes in release 2.5.4, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-2719](https://jira.mariadb.org/browse/MXS-2719) Allow queuing of switchover/failover
* [MXS-2692](https://jira.mariadb.org/browse/MXS-2692) Monitor of binlog router

## Bug fixes

* [MXS-3202](https://jira.mariadb.org/browse/MXS-3202) Additional grants not mentioned in upgrading document
* [MXS-3196](https://jira.mariadb.org/browse/MXS-3196) Errors can be returned in wrong order
* [MXS-3194](https://jira.mariadb.org/browse/MXS-3194) Reply with an error if a replica tries to connect with log_file/log_pos
* [MXS-3191](https://jira.mariadb.org/browse/MXS-3191) New binlog router should clearly says that it does not file/pos based options in CHANGE MASTER
* [MXS-3181](https://jira.mariadb.org/browse/MXS-3181) Binlog Router should understand all monitor queries
* [MXS-3177](https://jira.mariadb.org/browse/MXS-3177) Fix download link in documentation
* [MXS-3176](https://jira.mariadb.org/browse/MXS-3176) MaxGUI logs admin user out after dbfwfilter is added to MaxScale config file
* [MXS-3175](https://jira.mariadb.org/browse/MXS-3175) Make pinloki deployment simple and observeable
* [MXS-3171](https://jira.mariadb.org/browse/MXS-3171) PHP 7.2 + PDO - invalid HandShakeResponse
* [MXS-3167](https://jira.mariadb.org/browse/MXS-3167) Access Denied for User Error when Upgrading to 2.5.x and Using Binlogrouter
* [MXS-3165](https://jira.mariadb.org/browse/MXS-3165) cache_inside_transactions vs cache_in_transactions
* [MXS-3164](https://jira.mariadb.org/browse/MXS-3164) Handle AuthSwitchRequest-packet properly in standard auth plugin
* [MXS-3163](https://jira.mariadb.org/browse/MXS-3163) Wrong type for server and global modules
* [MXS-3160](https://jira.mariadb.org/browse/MXS-3160) PLUGIN_AUTH_LENENC_CLIENT_DATA capability not set
* [MXS-3150](https://jira.mariadb.org/browse/MXS-3150) MaxGUI should redirect to 404 page if id of details route doesn't exist 
* [MXS-3149](https://jira.mariadb.org/browse/MXS-3149) Monitor should remove [Master] when starting swithover
* [MXS-3148](https://jira.mariadb.org/browse/MXS-3148) Binlogrouter doesn't accept trailing semicolons
* [MXS-3147](https://jira.mariadb.org/browse/MXS-3147) Pinloki should not create logically empty binlog files.
* [MXS-3143](https://jira.mariadb.org/browse/MXS-3143) FOUND_ROWS() not routed to previous target
* [MXS-3140](https://jira.mariadb.org/browse/MXS-3140) Deleting a resource causes navigating to login page.
* [MXS-3130](https://jira.mariadb.org/browse/MXS-3130) Documentation for the source parameter is inaccurate
* [MXS-3110](https://jira.mariadb.org/browse/MXS-3110) bad handshake error in maxscale 2.5.1
* [MXS-2680](https://jira.mariadb.org/browse/MXS-2680) Monitor type not shown in show monitors 
* [MXS-1998](https://jira.mariadb.org/browse/MXS-1998) Shard map refresh is not logged on any level

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
