# MariaDB MaxScale 23.02.4 Release Notes -- 2023-08-30

Release 23.02.4 is a GA release.

This document describes the changes in release 23.02.4, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-23.02.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-4730](https://jira.mariadb.org/browse/MXS-4730) GUI default value of inputs in the object creation dialog is empty after closing the dialog
* [MXS-4726](https://jira.mariadb.org/browse/MXS-4726) Session command response verification unnecessarily stores PS IDs for readconnroute
* [MXS-4722](https://jira.mariadb.org/browse/MXS-4722) Case-sensitiveness of enumerations is not documented
* [MXS-4721](https://jira.mariadb.org/browse/MXS-4721) Galeramon does not update replication lag of replicating servers
* [MXS-4720](https://jira.mariadb.org/browse/MXS-4720) Implement an option to switch to the old "ping" behaviour in MaxScale
* [MXS-4719](https://jira.mariadb.org/browse/MXS-4719) Connection init sql file execution can hang
* [MXS-4717](https://jira.mariadb.org/browse/MXS-4717) information_schema is not invalidated as needed
* [MXS-4714](https://jira.mariadb.org/browse/MXS-4714) qc_sqlite does not properly parse a RENAME statement
* [MXS-4708](https://jira.mariadb.org/browse/MXS-4708) Update maxscale.cnf default file
* [MXS-4706](https://jira.mariadb.org/browse/MXS-4706) Cache does not invalidate when a table is ALTERed, DROPed or RENAMEd
* [MXS-4704](https://jira.mariadb.org/browse/MXS-4704) SHOW TABLE STATUS FROM some_schema Fails with SchemaRouter
* [MXS-4701](https://jira.mariadb.org/browse/MXS-4701) GTID update may block the REST-API
* [MXS-4700](https://jira.mariadb.org/browse/MXS-4700) Binlogrouter treats GTID sequences as 32-bit integers
* [MXS-4696](https://jira.mariadb.org/browse/MXS-4696) Readwritesplit does not detect unrecoverable situations
* [MXS-4694](https://jira.mariadb.org/browse/MXS-4694) Update MaxGUI screenshots in the documentation
* [MXS-4691](https://jira.mariadb.org/browse/MXS-4691) Binlogrouter cannot write binlog files larger than 4GiB
* [MXS-4690](https://jira.mariadb.org/browse/MXS-4690) Binlogrouter runs out of memory on very large transactions
* [MXS-4685](https://jira.mariadb.org/browse/MXS-4685) Replication via binlogrouter temporarily blocks the REST-API
* [MXS-4677](https://jira.mariadb.org/browse/MXS-4677) MaxScale BinlogRouter skips large transactions causing data Inconsistency on attached slave
* [MXS-4675](https://jira.mariadb.org/browse/MXS-4675) Switchover fails with 'Unknown thread id' error
* [MXS-4674](https://jira.mariadb.org/browse/MXS-4674) Crash in mxs1687_avro_ha
* [MXS-4668](https://jira.mariadb.org/browse/MXS-4668) Binlogrouter eventually stops working if semi-sync replication is not used

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
