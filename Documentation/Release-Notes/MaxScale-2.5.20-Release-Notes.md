# MariaDB MaxScale 2.5.20 Release Notes

Release 2.5.20 is a GA release.

This document describes the changes in release 2.5.20, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-2.5.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-3997](https://jira.mariadb.org/browse/MXS-3997) Name threads for better CPU usage view
* [MXS-3665](https://jira.mariadb.org/browse/MXS-3665) Provide more feedback on TLS cipher  mismatch

## Bug fixes

* [MXS-4110](https://jira.mariadb.org/browse/MXS-4110) Schemarouter does not ignore the sys schema
* [MXS-4109](https://jira.mariadb.org/browse/MXS-4109) The /user/inet endpoint fails schema validation
* [MXS-4103](https://jira.mariadb.org/browse/MXS-4103) Binlogrouter doesn't decrypt passwords
* [MXS-4096](https://jira.mariadb.org/browse/MXS-4096) Binlog Routers SHOW SLAVE STATUS does not show SSL information
* [MXS-4093](https://jira.mariadb.org/browse/MXS-4093) User account manager does not detect db-level priv changes
* [MXS-4092](https://jira.mariadb.org/browse/MXS-4092) schemarouter: duplicate tables found, if table differs on  upper lower case only
* [MXS-4086](https://jira.mariadb.org/browse/MXS-4086) REST API allows deletion of last user
* [MXS-4074](https://jira.mariadb.org/browse/MXS-4074) Status of boostrap servers not always the same as the status of corresponding runtime servers
* [MXS-4053](https://jira.mariadb.org/browse/MXS-4053) The cache does not handle multi-statements properly.
* [MXS-4045](https://jira.mariadb.org/browse/MXS-4045) Add maxctrl command for dumping the whole REST API output
* [MXS-4040](https://jira.mariadb.org/browse/MXS-4040) Mariadbmon constantly logs errors if event scheduler is disabled
* [MXS-4039](https://jira.mariadb.org/browse/MXS-4039) Debug assert with connection_keepalive and slow server
* [MXS-4038](https://jira.mariadb.org/browse/MXS-4038) maxctrl reload service does not bypass the users refresh rate limit
* [MXS-4034](https://jira.mariadb.org/browse/MXS-4034) Persistent connection pool is not automatically flushed
* [MXS-4024](https://jira.mariadb.org/browse/MXS-4024) CDC protocol logs a notice message for each new connection
* [MXS-4023](https://jira.mariadb.org/browse/MXS-4023) Schema auto-generation is not documented
* [MXS-4022](https://jira.mariadb.org/browse/MXS-4022) Avrorouter doesn't log an error for failed SHOW CREATE TABLE
* [MXS-4008](https://jira.mariadb.org/browse/MXS-4008) Query classifier cache does not properly record all used memory
* [MXS-4004](https://jira.mariadb.org/browse/MXS-4004) Race condition in KILL command execution
* [MXS-4003](https://jira.mariadb.org/browse/MXS-4003) GSSAPI authenticator documentation is out of date
* [MXS-4002](https://jira.mariadb.org/browse/MXS-4002) KILL commands leave no trace in the log
* [MXS-4001](https://jira.mariadb.org/browse/MXS-4001) The Cache filter cannot cope with the Redis server closing the connection
* [MXS-4000](https://jira.mariadb.org/browse/MXS-4000) Binlogrouter creates malformed replication events
* [MXS-3954](https://jira.mariadb.org/browse/MXS-3954) Got below signal 11 error after upgrading maxscale version  maxscale 6.2.1
* [MXS-3945](https://jira.mariadb.org/browse/MXS-3945) Sync marker mismatch while reading Avro file
* [MXS-3931](https://jira.mariadb.org/browse/MXS-3931) Check certificates with extendedKeyUsage options set for correct purpose flags
* [MXS-3808](https://jira.mariadb.org/browse/MXS-3808) Improve Rest API performance

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
