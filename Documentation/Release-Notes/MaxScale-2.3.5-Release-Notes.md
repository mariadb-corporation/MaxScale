# MariaDB MaxScale 2.3.5 Release Notes

Release 2.3.5 is a GA release.

This document describes the changes in release 2.3.5, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-2412](https://jira.mariadb.org/browse/MXS-2412) Service serialization is broken
* [MXS-2410](https://jira.mariadb.org/browse/MXS-2410) Hangup delivered to wrong DCB
* [MXS-2409](https://jira.mariadb.org/browse/MXS-2409) Schemarouter crashes if PREPARE or EXECUTE is malformed
* [MXS-2403](https://jira.mariadb.org/browse/MXS-2403) Masking filter should check subqueries
* [MXS-2402](https://jira.mariadb.org/browse/MXS-2402) Masking filter should check unions
* [MXS-2398](https://jira.mariadb.org/browse/MXS-2398) Recognize MariaDB specific executable comments
* [MXS-2396](https://jira.mariadb.org/browse/MXS-2396) Masking filter should examine user variables
* [MXS-2394](https://jira.mariadb.org/browse/MXS-2394) global setting "substitute_variables" is rejected as unknown global parameter
* [MXS-2393](https://jira.mariadb.org/browse/MXS-2393) Masking filter should check parse result of query classification.
* [MXS-2392](https://jira.mariadb.org/browse/MXS-2392) Masking filter should examine statement being prepared
* [MXS-2390](https://jira.mariadb.org/browse/MXS-2390) Masking and DBFW filters should reject statement prepared from variable
* [MXS-2389](https://jira.mariadb.org/browse/MXS-2389) Fix executable comment handling
* [MXS-2379](https://jira.mariadb.org/browse/MXS-2379) JSON Interface not work with Maxscale 2.3
* [MXS-2374](https://jira.mariadb.org/browse/MXS-2374) Binlogfilter can break replication if last event is ignored
* [MXS-2373](https://jira.mariadb.org/browse/MXS-2373) Generated configs for filters does not include module
* [MXS-2370](https://jira.mariadb.org/browse/MXS-2370) Query timeout warning message does not print reason of timeout
* [MXS-2368](https://jira.mariadb.org/browse/MXS-2368) maxctrl requires password on command line and cannot change user password
* [MXS-2365](https://jira.mariadb.org/browse/MXS-2365) Wrong classification of queued queries in readwritesplit
* [MXS-2359](https://jira.mariadb.org/browse/MXS-2359) LIKE clause in SHOW TABLES is ignored by schemarouter
* [MXS-2357](https://jira.mariadb.org/browse/MXS-2357) maxctrl documentation for alter service, include use_sql_variables_in
* [MXS-2355](https://jira.mariadb.org/browse/MXS-2355) MaxScale does not let mysql client 8.0.15 to connect with password
* [MXS-2342](https://jira.mariadb.org/browse/MXS-2342) maxadmin commands hang when master pod deleted after failover occurs
* [MXS-2337](https://jira.mariadb.org/browse/MXS-2337) schemarouter in 2.3.4 doesn't show all tables from all backends
* [MXS-2326](https://jira.mariadb.org/browse/MXS-2326) Routing hints are ignored when reconnection is required
* [MXS-2325](https://jira.mariadb.org/browse/MXS-2325) Disabled events are enabled on promoted slave upon failover
* [MXS-2323](https://jira.mariadb.org/browse/MXS-2323) Connections to servers in maintenance aren't closed
* [MXS-2292](https://jira.mariadb.org/browse/MXS-2292) Allow PAM user and group mapping to work with more specific host than '%'
* [MXS-1991](https://jira.mariadb.org/browse/MXS-1991) Why MariaDBMon complain about replication_user and replication_password?

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
