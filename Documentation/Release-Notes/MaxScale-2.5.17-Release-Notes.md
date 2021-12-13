# MariaDB MaxScale 2.5.17 Release Notes -- 2021-12-13

Release 2.5.17 is a GA release.

This document describes the changes in release 2.5.17, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-2.5.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3885](https://jira.mariadb.org/browse/MXS-3885) MaxScale unconditionally loads global options from /etc/maxscale.cnf.d/maxscale.cnf
* [MXS-3872](https://jira.mariadb.org/browse/MXS-3872) test_kafkacdc fails in 2.5
* [MXS-3871](https://jira.mariadb.org/browse/MXS-3871) Kerberos tests are skipped in 2.5
* [MXS-3870](https://jira.mariadb.org/browse/MXS-3870) The pam_authentication_2fa test fails very often
* [MXS-3858](https://jira.mariadb.org/browse/MXS-3858) core dumps from system-tests (meta bug)
* [MXS-3857](https://jira.mariadb.org/browse/MXS-3857) Pinloki initial gtid scan incorrectly reads entire files
* [MXS-3856](https://jira.mariadb.org/browse/MXS-3856) Errors with causal_reads and read-only transactions
* [MXS-3845](https://jira.mariadb.org/browse/MXS-3845) Sending binlog events is inefficient
* [MXS-3832](https://jira.mariadb.org/browse/MXS-3832) Document privileges required for procs_priv system table
* [MXS-3826](https://jira.mariadb.org/browse/MXS-3826) Allow maintenance mode to be set on Galera cluster master
* [MXS-3824](https://jira.mariadb.org/browse/MXS-3824) Allow symbolic link for path to directory /usr/share/maxscale/gui
* [MXS-3817](https://jira.mariadb.org/browse/MXS-3817) The location of the GUI web directory isn't documented
* [MXS-3816](https://jira.mariadb.org/browse/MXS-3816) Queries are not always counted as reads with router_options=slave
* [MXS-3815](https://jira.mariadb.org/browse/MXS-3815) maxscale crash
* [MXS-3814](https://jira.mariadb.org/browse/MXS-3814) maxscale rpl_state is empty
* [MXS-3810](https://jira.mariadb.org/browse/MXS-3810) SQL_MODE parsing sometimes fails
* [MXS-3809](https://jira.mariadb.org/browse/MXS-3809) When MariaDBMonitor acquires lock majority, the log message gives the impression that auto_failover is enabled even when it is not configured
* [MXS-3801](https://jira.mariadb.org/browse/MXS-3801) Unexpected internal state with read-only cursor and result with one row
* [MXS-3800](https://jira.mariadb.org/browse/MXS-3800) Not enough information in server state change messages
* [MXS-3782](https://jira.mariadb.org/browse/MXS-3782) session_track_trx_state set to true causes incorrect routing of SELECT

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
