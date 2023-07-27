# MariaDB MaxScale 2.5.27 Release Notes -- 2023-07-27

Release 2.5.27 is a GA release.

This document describes the changes in release 2.5.27, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-2.5.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-4541](https://jira.mariadb.org/browse/MXS-4541) Provide a way to show details about all supported MaxScale modules via REST API and/or MaxCtrl

## Bug fixes

* [MXS-4676](https://jira.mariadb.org/browse/MXS-4676) REST-API documentation is wrong about which server parameters can be modified
* [MXS-4670](https://jira.mariadb.org/browse/MXS-4670) The fact that readconnroute doesn't block writes with router_options=slave is not documented
* [MXS-4665](https://jira.mariadb.org/browse/MXS-4665) Listener creation error is misleading
* [MXS-4659](https://jira.mariadb.org/browse/MXS-4659) Cache filter hangs if statement consists of multiple packets.
* [MXS-4657](https://jira.mariadb.org/browse/MXS-4657) Add human readable message text to API errors like 404
* [MXS-4656](https://jira.mariadb.org/browse/MXS-4656) Setting session_track_trx_state=true leads to OOM kiled.
* [MXS-4642](https://jira.mariadb.org/browse/MXS-4642) Document that the Xpand service-user requires "show databases" privilege
* [MXS-4617](https://jira.mariadb.org/browse/MXS-4617) expire_log_duration not working

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
