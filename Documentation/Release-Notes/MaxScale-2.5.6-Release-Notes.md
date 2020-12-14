# MariaDB MaxScale 2.5.6 Release Notes

Release 2.5.6 is a GA release.

This document describes the changes in release 2.5.6, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-3129](https://jira.mariadb.org/browse/MXS-3129) Add Switchover to WebGUI

## Bug fixes

* [MXS-3338](https://jira.mariadb.org/browse/MXS-3338) Last statement in read-only transaction doesn't trigger transaction replay
* [MXS-3335](https://jira.mariadb.org/browse/MXS-3335) MaxScale 2.5.6 and Xpand Direct connection issue with xpandmon 
* [MXS-3334](https://jira.mariadb.org/browse/MXS-3334) Master failure triggers replay of read-only transaction
* [MXS-3333](https://jira.mariadb.org/browse/MXS-3333) Malformed error messages
* [MXS-3328](https://jira.mariadb.org/browse/MXS-3328) Persistent Connections on Maxscale 2.5 seem to break client authentication
* [MXS-3327](https://jira.mariadb.org/browse/MXS-3327) Memcachd/Redis cache storage must be disabled if memcached/redis not reachable
* [MXS-3326](https://jira.mariadb.org/browse/MXS-3326) Host class does not accept all valid domain names.
* [MXS-3325](https://jira.mariadb.org/browse/MXS-3325) Redis cache storage does not accept dashes in server names.
* [MXS-3316](https://jira.mariadb.org/browse/MXS-3316) Redis cache storage must be disabled if Redis not connectible 
* [MXS-3313](https://jira.mariadb.org/browse/MXS-3313) MaxScale cache must use timeout when connecting to redis/memcached 
* [MXS-3312](https://jira.mariadb.org/browse/MXS-3312) MaxScale not allowing login and sometimes crashes when cache server not available
* [MXS-3311](https://jira.mariadb.org/browse/MXS-3311) routed_packets not updated by readconnroute
* [MXS-3307](https://jira.mariadb.org/browse/MXS-3307) Multiple binlogrouters in the same MaxScale
* [MXS-3301](https://jira.mariadb.org/browse/MXS-3301) MaxScale does not recognize xpand properly
* [MXS-3295](https://jira.mariadb.org/browse/MXS-3295) Layout of classify REST API endpoint stores non-parameter data in parameters object
* [MXS-3293](https://jira.mariadb.org/browse/MXS-3293) Backticks not stripped in USE statements.
* [MXS-3292](https://jira.mariadb.org/browse/MXS-3292) Unable to execute  use `database`
* [MXS-3283](https://jira.mariadb.org/browse/MXS-3283) Scramble should be composed of characters
* [MXS-3282](https://jira.mariadb.org/browse/MXS-3282) Select query inside transactions are routed to slave with session_track_trx_state=true
* [MXS-3272](https://jira.mariadb.org/browse/MXS-3272) maxctrl not prompt directy for the password
* [MXS-3271](https://jira.mariadb.org/browse/MXS-3271) dump_last_statements=on_close doesn't log session ID
* [MXS-3270](https://jira.mariadb.org/browse/MXS-3270) MaxScale 2.5.5 crashes with signal 11
* [MXS-3264](https://jira.mariadb.org/browse/MXS-3264) The event mechanism is not configured.
* [MXS-3256](https://jira.mariadb.org/browse/MXS-3256) Match pinloki initial setup to that of MariaDB server
* [MXS-3251](https://jira.mariadb.org/browse/MXS-3251) Hang on shutdown when executing KILL query
* [MXS-3248](https://jira.mariadb.org/browse/MXS-3248) "error  : (1672) Unexpected result state" with connection_keepalive
* [MXS-3199](https://jira.mariadb.org/browse/MXS-3199) KafkaCDC read stream data too slow
* [MXS-3172](https://jira.mariadb.org/browse/MXS-3172) Database grants with escape characters do not work (strip_db_esc)

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
