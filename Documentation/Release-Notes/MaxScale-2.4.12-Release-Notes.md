# MariaDB MaxScale 2.4.12 Release Notes

Release 2.4.12 is a GA release.

This document describes the changes in release 2.4.12, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3121](https://jira.mariadb.org/browse/MXS-3121) At SIGSEGV time, the statement currently being parsed should be logged.
* [MXS-3120](https://jira.mariadb.org/browse/MXS-3120) Crash in qc_sqlite.
* [MXS-3115](https://jira.mariadb.org/browse/MXS-3115) Error loading kubernetes mounted cnf
* [MXS-3113](https://jira.mariadb.org/browse/MXS-3113) No message in new log after rotate
* [MXS-3106](https://jira.mariadb.org/browse/MXS-3106) 'Cannot assign requested address'
* [MXS-3101](https://jira.mariadb.org/browse/MXS-3101) getpeername()' failed on file descriptor
* [MXS-3100](https://jira.mariadb.org/browse/MXS-3100) Potential memory leak
* [MXS-3089](https://jira.mariadb.org/browse/MXS-3089) Backend not closed on failed session command
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
