# MariaDB MaxScale 2.3.7 Release Notes

Release 2.3.7 is a GA release.

This document describes the changes in release 2.3.7, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-1469](https://jira.mariadb.org/browse/MXS-1469) Galeramon: Server-side initiated maintenance mode

## Bug fixes

* [MXS-2475](https://jira.mariadb.org/browse/MXS-2475) Fix mxs1980_blr_galera_server_ids
* [MXS-2474](https://jira.mariadb.org/browse/MXS-2474) Housekeeper allows the registration of the same task multiple times
* [MXS-2472](https://jira.mariadb.org/browse/MXS-2472) BinlogRouter's "Using secondary masters" documentation is incomplete
* [MXS-2457](https://jira.mariadb.org/browse/MXS-2457) MaxScale Mask Filter incorrectly handles ANSI_QUOTES
* [MXS-2450](https://jira.mariadb.org/browse/MXS-2450) Crash on COM_CHANGE_USER with disable_sescmd_history=true
* [MXS-2433](https://jira.mariadb.org/browse/MXS-2433) infinite memory usage, possible leak
* [MXS-2427](https://jira.mariadb.org/browse/MXS-2427) namedserverfilter fails to direct query if first server listed in targetXY is in maintenance mode
* [MXS-2415](https://jira.mariadb.org/browse/MXS-2415) data for new AVRO versions is not send via cdc
* [MXS-2381](https://jira.mariadb.org/browse/MXS-2381) Admin user passwords cannot be changed
* [MXS-2366](https://jira.mariadb.org/browse/MXS-2366) Wrong tarball RPATH
* [MXS-2315](https://jira.mariadb.org/browse/MXS-2315) std::regex_error exception on csmon startup
* [MXS-2046](https://jira.mariadb.org/browse/MXS-2046) Memory leak in binlog router

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for supported the Linux distributions.

Packages can be downloaded [here](https://mariadb.com/downloads/mariadb-tx/maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is `maxscale-X.Y.Z`. Further, the default branch is always the latest GA version
of MaxScale.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
