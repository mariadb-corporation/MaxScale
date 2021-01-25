# MariaDB MaxScale 2.5.7 Release Notes

Release 2.5.7 is a GA release.

This document describes the changes in release 2.5.7, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-2.5.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-3193](https://jira.mariadb.org/browse/MXS-3193) Allow the binlog router to replicate from Galera cluster with failover.

## Bug fixes

* [MXS-3374](https://jira.mariadb.org/browse/MXS-3374) MaxScale fails to update IP for a existing node that reappears with a IP change  
* [MXS-3370](https://jira.mariadb.org/browse/MXS-3370) Asynchronicity of tee filter isn't documented
* [MXS-3365](https://jira.mariadb.org/browse/MXS-3365) Missing match setting in filter(s) results in no match at all
* [MXS-3360](https://jira.mariadb.org/browse/MXS-3360) MaxCtrl option --authenticator-options doesn't work
* [MXS-3354](https://jira.mariadb.org/browse/MXS-3354) Duplicate space interpreted as a command
* [MXS-3353](https://jira.mariadb.org/browse/MXS-3353) Tee filter loses statements if branch target is slower
* [MXS-3348](https://jira.mariadb.org/browse/MXS-3348) KafkaCDC :Failed to read replicated event
* [MXS-3347](https://jira.mariadb.org/browse/MXS-3347) Hint syntax documentation isn't exact
* [MXS-3346](https://jira.mariadb.org/browse/MXS-3346) When using --basedir, mysql/plugin dir needs to be writable
* [MXS-3342](https://jira.mariadb.org/browse/MXS-3342) Persistent connections cause signal 11  when no matching connection is found
* [MXS-3339](https://jira.mariadb.org/browse/MXS-3339) Hang with prepared statement and slow servers
* [MXS-3337](https://jira.mariadb.org/browse/MXS-3337) galeramon queries only status, not variables
* [MXS-3323](https://jira.mariadb.org/browse/MXS-3323) Database grants with wildcards should be matched using LIKE operator
* [MXS-3318](https://jira.mariadb.org/browse/MXS-3318) Parsing error with comment
* [MXS-3309](https://jira.mariadb.org/browse/MXS-3309) The administration tutorial is lacking
* [MXS-3303](https://jira.mariadb.org/browse/MXS-3303) A user with just EXECUTE on PROCEDURE privileges to the database, failing to connect via maxscale with database name mentioned.
* [MXS-3238](https://jira.mariadb.org/browse/MXS-3238) REST API SSL Configuration Vulnerabilities
* [MXS-3158](https://jira.mariadb.org/browse/MXS-3158) Failover/switchover modifies event character set and collation
* [MXS-2627](https://jira.mariadb.org/browse/MXS-2627) Document how to customize MaxScale's systemd unit file

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
