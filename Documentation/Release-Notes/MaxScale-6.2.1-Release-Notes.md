# MariaDB MaxScale 6.2.1 Release Notes

Release 6.2.1 is a GA release.

This document describes the changes in release 6.2.1, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-6.2.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-3894](https://jira.mariadb.org/browse/MXS-3894) Invent a configuration option to allow transaction replay ignore checksum check
* [MXS-3379](https://jira.mariadb.org/browse/MXS-3379) Make transaction_replay less strict
* [MXS-2353](https://jira.mariadb.org/browse/MXS-2353) Per service log_info

## Bug fixes

* [MXS-3943](https://jira.mariadb.org/browse/MXS-3943) Altering xpandmon at runtime fails
* [MXS-3941](https://jira.mariadb.org/browse/MXS-3941) create_one_connection_for_sescmd() doesn't correctly replace m_current_master
* [MXS-3940](https://jira.mariadb.org/browse/MXS-3940) Debug assertion in mariadb_backend.cc
* [MXS-3939](https://jira.mariadb.org/browse/MXS-3939) Debug assertion during transaction replay
* [MXS-3938](https://jira.mariadb.org/browse/MXS-3938) Debug assert in xpandmon
* [MXS-3937](https://jira.mariadb.org/browse/MXS-3937) Transaction replay time limits are unpredictable
* [MXS-3934](https://jira.mariadb.org/browse/MXS-3934) Linking a service at runtime to an xpandmon doesn't work
* [MXS-3929](https://jira.mariadb.org/browse/MXS-3929) connection stalled after executing a stored procedure with OUT parameter
* [MXS-3928](https://jira.mariadb.org/browse/MXS-3928) MaxScale logs a warning when users are loaded from a Xpand cluster
* [MXS-3927](https://jira.mariadb.org/browse/MXS-3927) Some log messages do not contain the session ID
* [MXS-3924](https://jira.mariadb.org/browse/MXS-3924) Session commands are not retried with delayed_retry
* [MXS-3920](https://jira.mariadb.org/browse/MXS-3920) Can't connect to MaxScale when schema uses utf8mb4 chars >= U0080
* [MXS-3917](https://jira.mariadb.org/browse/MXS-3917) Crash during `set server maintenance --force`
* [MXS-3915](https://jira.mariadb.org/browse/MXS-3915) Autocommit tracking doesn't work correctly
* [MXS-3911](https://jira.mariadb.org/browse/MXS-3911) Monitor parameters table is not modifiable (GUI)
* [MXS-3909](https://jira.mariadb.org/browse/MXS-3909) Skip http_proxy when address is localhost
* [MXS-3908](https://jira.mariadb.org/browse/MXS-3908) MaxScale crashes (double free or corruption)
* [MXS-3907](https://jira.mariadb.org/browse/MXS-3907) Unexpected result state
* [MXS-3900](https://jira.mariadb.org/browse/MXS-3900) Add multi-threaded stack traces
* [MXS-3897](https://jira.mariadb.org/browse/MXS-3897) MaxScale crashes when executing CDC process to kafka
* [MXS-3896](https://jira.mariadb.org/browse/MXS-3896) When reading password from stdin via redirect, interactive use is no longer possible
* [MXS-3893](https://jira.mariadb.org/browse/MXS-3893) read/write split service incorrectly times out valid sessions on master if timeout happens on replica
* [MXS-3841](https://jira.mariadb.org/browse/MXS-3841) LIMIT should be added for each select query automatically
* [MXS-3807](https://jira.mariadb.org/browse/MXS-3807) Using the binlog router as the source for KafkaCDC router is unreliable
* [MXS-3544](https://jira.mariadb.org/browse/MXS-3544) Use virtual scroll on maxscale log view to fix memory issue

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
