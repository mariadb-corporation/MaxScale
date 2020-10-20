# MariaDB MaxScale 2.5.5 Release Notes

Release 2.5.5 is a GA release.

This document describes the changes in release 2.5.5, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3244](https://jira.mariadb.org/browse/MXS-3244) Default the binlogrouter master port to 3306 (CHANGE MASTER TO)
* [MXS-3242](https://jira.mariadb.org/browse/MXS-3242) Hang on repeated master disconnection
* [MXS-3235](https://jira.mariadb.org/browse/MXS-3235) Insertstream streams autocommit inserts
* [MXS-3229](https://jira.mariadb.org/browse/MXS-3229) Hang with COM_SET_OPTION
* [MXS-3223](https://jira.mariadb.org/browse/MXS-3223) Monitor re-uses mysql-handle when using extra-port
* [MXS-3218](https://jira.mariadb.org/browse/MXS-3218) Crash with LOAD DATA LOCAL INFILE
* [MXS-3212](https://jira.mariadb.org/browse/MXS-3212) Server SSL configuration cannot be defined at runtime
* [MXS-3200](https://jira.mariadb.org/browse/MXS-3200) Abort due to double free or corruption after "Write to Client DCB ... state DCB_STATE_POLLING failed"
* [MXS-3198](https://jira.mariadb.org/browse/MXS-3198) MariadbMon documentation needs to take 10.5 privilege changes into account
* [MXS-3157](https://jira.mariadb.org/browse/MXS-3157) Write MaxGUI system test
* [MXS-3142](https://jira.mariadb.org/browse/MXS-3142) Misleading error when SSL not used when it is required

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
