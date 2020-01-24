# MariaDB MaxScale 2.4.6 Release Notes -- 2020-01-24

Release 2.4.6 is a GA release.

This document describes the changes in release 2.4.6, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-2758](https://jira.mariadb.org/browse/MXS-2758) Enable the systemd unit after setting up the maxscale package

## Bug fixes

* [MXS-2843](https://jira.mariadb.org/browse/MXS-2843) remove password info from maxscale log
* [MXS-2839](https://jira.mariadb.org/browse/MXS-2839) Not configuring ssl_ca_cert should use system CA
* [MXS-2834](https://jira.mariadb.org/browse/MXS-2834) Transaction retrying on deadlock is not configurable
* [MXS-2829](https://jira.mariadb.org/browse/MXS-2829) Deleting a filter doesn't remove the persisted file
* [MXS-2824](https://jira.mariadb.org/browse/MXS-2824) Basic user access is not documented from maxctrl's perspective
* [MXS-2821](https://jira.mariadb.org/browse/MXS-2821) Generic error message "Invalid object relations for" when dynamically creating or linking items in maxscale with maxctrl
* [MXS-2820](https://jira.mariadb.org/browse/MXS-2820) Wrong error message for failed authentication
* [MXS-2815](https://jira.mariadb.org/browse/MXS-2815) [Warning] Platform does not support O_DIRECT in conjunction with pipes - Pls. downgrade to [Notice]
* [MXS-2812](https://jira.mariadb.org/browse/MXS-2812) "Auth Error" server status only documented in 1.4 documentation
* [MXS-2810](https://jira.mariadb.org/browse/MXS-2810) maxscale process still running after uninstalling maxscale package
* [MXS-2801](https://jira.mariadb.org/browse/MXS-2801) session_trace doesn't contain all messages
* [MXS-2798](https://jira.mariadb.org/browse/MXS-2798) MXS-1550 (net_write_timeout) is not documented
* [MXS-2792](https://jira.mariadb.org/browse/MXS-2792) Documentation for MaxScale's monitor script is incomplete
* [MXS-2710](https://jira.mariadb.org/browse/MXS-2710) max_connections problem
* [MXS-619](https://jira.mariadb.org/browse/MXS-619) creating many short sessions in parallel leads to errors

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
