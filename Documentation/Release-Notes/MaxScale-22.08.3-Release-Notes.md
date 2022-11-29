# MariaDB MaxScale 22.08.3 Release Notes

Release 22.08.3 is a GA release.

This document describes the changes in release 22.08.3, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-22.08.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-4415](https://jira.mariadb.org/browse/MXS-4415) Warning for xpandmon for 'leaving' / 'late, leaving' being an unknown sub-state
* [MXS-4413](https://jira.mariadb.org/browse/MXS-4413) UPDATE with user variable breaks replication
* [MXS-4406](https://jira.mariadb.org/browse/MXS-4406) State Shown was Not Correct by 'maxctrl list servers' Command
* [MXS-4404](https://jira.mariadb.org/browse/MXS-4404) Maxscale: KafkaCDC writes to current_gtid.txt causes high disk utilisation.
* [MXS-4399](https://jira.mariadb.org/browse/MXS-4399) Query editor - Keyboard trap (accessibility)
* [MXS-4397](https://jira.mariadb.org/browse/MXS-4397) fields parameter breaks REST-API filtering
* [MXS-4393](https://jira.mariadb.org/browse/MXS-4393) Authentication failures during shard mapping are not handled correctly
* [MXS-4392](https://jira.mariadb.org/browse/MXS-4392) Rebuild-server should read gtid from xtrabackup_binlog_info-file
* [MXS-4389](https://jira.mariadb.org/browse/MXS-4389) Crash in handleError
* [MXS-4388](https://jira.mariadb.org/browse/MXS-4388) LOAD DATA LOCAL INFILE and changing autocomit causing stuck
* [MXS-4378](https://jira.mariadb.org/browse/MXS-4378) Obsolete "Run" button label
* [MXS-4369](https://jira.mariadb.org/browse/MXS-4369) Save Script Is Not Working
* [MXS-4365](https://jira.mariadb.org/browse/MXS-4365) Query tab is going infinite loop, unable to use the query tab after the infinite loop
* [MXS-4360](https://jira.mariadb.org/browse/MXS-4360) New values on the GUI table aren't updated until refresh the browser
* [MXS-4358](https://jira.mariadb.org/browse/MXS-4358) Query editor doesn't redirect to the login page after auth token is expired
* [MXS-4356](https://jira.mariadb.org/browse/MXS-4356) Query classification sometimes treats table names as constants
* [MXS-4352](https://jira.mariadb.org/browse/MXS-4352) Debug assertion when server deleted with persist_runtime_changes=false
* [MXS-4347](https://jira.mariadb.org/browse/MXS-4347)  error: no matching constructor for initialization of 'maxsql::ComPacket'
* [MXS-4317](https://jira.mariadb.org/browse/MXS-4317) Smartrouter interrupts the wrong query
* [MXS-4301](https://jira.mariadb.org/browse/MXS-4301) Allow case-insensitive [maxscale] section name
* [MXS-3043](https://jira.mariadb.org/browse/MXS-3043) Database grants in user_accounts_file should add the database to the list of known databases

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the supported Linux distributions.

Packages can be downloaded [here](https://mariadb.com/downloads/#mariadb_platform-mariadb_maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is `maxscale-X.Y.Z`. Further, the default branch is always the latest GA version
of MaxScale.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
