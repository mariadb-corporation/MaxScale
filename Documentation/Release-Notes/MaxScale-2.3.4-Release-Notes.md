# MariaDB MaxScale 2.3.4 Release Notes

Release 2.3.4 is a GA release.

This document describes the changes in release 2.3.4, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-2112](https://jira.mariadb.org/browse/MXS-2112) Create tool to gather a total system report

## Bug fixes

* [MXS-2322](https://jira.mariadb.org/browse/MXS-2322) Null characters in passwords break maxctrl
* [MXS-2315](https://jira.mariadb.org/browse/MXS-2315) std::regex_error exception on csmon startup
* [MXS-2310](https://jira.mariadb.org/browse/MXS-2310) Cannot create .avsc files if no database connection
* [MXS-2305](https://jira.mariadb.org/browse/MXS-2305) UNIX users are not shown in `maxctrl list users`
* [MXS-2303](https://jira.mariadb.org/browse/MXS-2303) Missing server parameters log wrong error
* [MXS-2300](https://jira.mariadb.org/browse/MXS-2300) Reconnection requirements are too strict
* [MXS-2299](https://jira.mariadb.org/browse/MXS-2299) Routing hints are ignored for multi-statement and SP calls
* [MXS-2295](https://jira.mariadb.org/browse/MXS-2295) COM_CHANGE_USER does not clear out session command history
* [MXS-2268](https://jira.mariadb.org/browse/MXS-2268) readwritesplitter is not routing queries properly in Maxscale 2.3.2
* [MXS-2265](https://jira.mariadb.org/browse/MXS-2265) Timestamp value '0000-00-00 00:00:00' conversion issue
* [MXS-2237](https://jira.mariadb.org/browse/MXS-2237) server status labels are not documented
* [MXS-2038](https://jira.mariadb.org/browse/MXS-2038) debug assert at rwsplit_route_stmt.cc:469 failed: btype != BE_MASTER

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
