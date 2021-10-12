# MariaDB MaxScale 2.5.16 Release Notes -- 2021-10-12

Release 2.5.16 is a GA release.

This document describes the changes in release 2.5.16, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-2.5.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3805](https://jira.mariadb.org/browse/MXS-3805) Binlogrouter error messages aren't specific enough
* [MXS-3804](https://jira.mariadb.org/browse/MXS-3804) Result size accounting is wrong
* [MXS-3799](https://jira.mariadb.org/browse/MXS-3799) Destroyed monitors are not deleted
* [MXS-3798](https://jira.mariadb.org/browse/MXS-3798) Race condition in service destruction
* [MXS-3790](https://jira.mariadb.org/browse/MXS-3790) Fix luafilter
* [MXS-3788](https://jira.mariadb.org/browse/MXS-3788) Debug assertion with default config and transaction_replay=true
* [MXS-3779](https://jira.mariadb.org/browse/MXS-3779) binlogrouter logs warnings for ignored SQL
* [MXS-3766](https://jira.mariadb.org/browse/MXS-3766) Not able to insert data on Masking enabled table 
* [MXS-3759](https://jira.mariadb.org/browse/MXS-3759) Client hangs forever when server failed or restarted
* [MXS-3756](https://jira.mariadb.org/browse/MXS-3756) KILL behavior is not well documented
* [MXS-3748](https://jira.mariadb.org/browse/MXS-3748) Crash when unified log cannot be created
* [MXS-3747](https://jira.mariadb.org/browse/MXS-3747) Empty strings aren't serialized as quoted strings
* [MXS-3746](https://jira.mariadb.org/browse/MXS-3746) type=listener is added twice in listener serialization
* [MXS-3738](https://jira.mariadb.org/browse/MXS-3738) maxctrl show dbusers does nothing
* [MXS-3734](https://jira.mariadb.org/browse/MXS-3734) show binlog error msg is incorrect
* [MXS-3728](https://jira.mariadb.org/browse/MXS-3728) Binlogrouter crashes when GTID is not found
* [MXS-3718](https://jira.mariadb.org/browse/MXS-3718) MaxScale killed by watchdog timeout
* [MXS-3657](https://jira.mariadb.org/browse/MXS-3657) CCR Filter ignores PCRE2 option ignorecase
* [MXS-3580](https://jira.mariadb.org/browse/MXS-3580) Avrorouter should store full GTID coordinates
* [MXS-3331](https://jira.mariadb.org/browse/MXS-3331) Could not bind connecting socket to local address
* [MXS-3298](https://jira.mariadb.org/browse/MXS-3298) DNS server failure crashes Maxscale
* [MXS-3254](https://jira.mariadb.org/browse/MXS-3254) Monitor failover fails
* [MXS-3063](https://jira.mariadb.org/browse/MXS-3063) error  : Sync marker mismatch.
* [MXS-3060](https://jira.mariadb.org/browse/MXS-3060) Failed to load current GTID
* [MXS-3050](https://jira.mariadb.org/browse/MXS-3050) Setting Up MaxScale documentation should include instructions on how to configure the MaxScale grants in ClustrixDB
* [MXS-3049](https://jira.mariadb.org/browse/MXS-3049) error  : [avrorouter] Reading Avro file failed with error 'MAXAVRO_ERR_VALUE_OVERFLOW'.

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
