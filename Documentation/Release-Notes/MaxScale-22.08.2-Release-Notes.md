# MariaDB MaxScale 22.08.2 Release Notes

Release 22.08.2 is a GA release.

This document describes the changes in release 22.08.2, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-22.08.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-4333](https://jira.mariadb.org/browse/MXS-4333) Allow engine selection for Kafka Importer
* [MXS-4271](https://jira.mariadb.org/browse/MXS-4271) Add OpenID integration for the REST API
* [MXS-4161](https://jira.mariadb.org/browse/MXS-4161) MaxScale System Diagnostics
* [MXS-4122](https://jira.mariadb.org/browse/MXS-4122) Fast Global causal_reads
* [MXS-4044](https://jira.mariadb.org/browse/MXS-4044) Log a message whenever a server is set into maintenance mode.
* [MXS-3946](https://jira.mariadb.org/browse/MXS-3946) Highlight text when filtering (GUI)
* [MXS-3822](https://jira.mariadb.org/browse/MXS-3822) MaxScale Global Memory Use Indicator
* [MXS-3649](https://jira.mariadb.org/browse/MXS-3649) Add service reload time somewhere in maxctrl output
* [MXS-3384](https://jira.mariadb.org/browse/MXS-3384) Able to list/group servers by Monitor name
* [MXS-3012](https://jira.mariadb.org/browse/MXS-3012) Allow for ranking of servers for failover

## Bug fixes

* [MXS-4353](https://jira.mariadb.org/browse/MXS-4353) /maxscale/logs/data endpoint doesn't filter syslog contents correctly
* [MXS-4350](https://jira.mariadb.org/browse/MXS-4350) The rebalancing functionality is claimed to be disabled
* [MXS-4349](https://jira.mariadb.org/browse/MXS-4349) The documentation of the behaviour of 'threads=auto' in containers is incorrect
* [MXS-4348](https://jira.mariadb.org/browse/MXS-4348) Full SASL support is not enabled for kafka modules
* [MXS-4338](https://jira.mariadb.org/browse/MXS-4338) History/Snippets filter doesn't work on the action and date columns
* [MXS-4325](https://jira.mariadb.org/browse/MXS-4325) Listeners do not accept ssl_ca
* [MXS-4321](https://jira.mariadb.org/browse/MXS-4321) Error from missing --secure option is not helpful
* [MXS-4316](https://jira.mariadb.org/browse/MXS-4316) Data preview tab shows 2 execution time
* [MXS-4313](https://jira.mariadb.org/browse/MXS-4313) MaxCtrl misinterprets some arguments
* [MXS-4312](https://jira.mariadb.org/browse/MXS-4312) REST API accepts empty resource IDs
* [MXS-4307](https://jira.mariadb.org/browse/MXS-4307) Parser can't recognize convert function parameters and cause wrong routing decision
* [MXS-4304](https://jira.mariadb.org/browse/MXS-4304) MariaDB-Monitor spams log with connection errors if server is both [Maintenance] and [Down]
* [MXS-4303](https://jira.mariadb.org/browse/MXS-4303) Disconnect connection in the reconnection dialog doesn't work properly
* [MXS-4299](https://jira.mariadb.org/browse/MXS-4299) Unsaved query tab isn't saved when the current active query tab is not the one has unsaved changes
* [MXS-4295](https://jira.mariadb.org/browse/MXS-4295) Switchover will wait even if slave server are lagging more than switchover_timeout
* [MXS-4293](https://jira.mariadb.org/browse/MXS-4293) Memory leak with causal_reads=universal
* [MXS-4292](https://jira.mariadb.org/browse/MXS-4292) Read-only transaction are not synchronized by causal_reads=universal
* [MXS-4291](https://jira.mariadb.org/browse/MXS-4291) Avrorouter crash when client is closed
* [MXS-4290](https://jira.mariadb.org/browse/MXS-4290) Maxscale masking filter returns parsing error on SELECT with very long WHERE
* [MXS-4289](https://jira.mariadb.org/browse/MXS-4289) Transaction starts on wrong server with autocommit=0
* [MXS-4281](https://jira.mariadb.org/browse/MXS-4281) Query editor connections aren't bound to the query tabs properly
* [MXS-4280](https://jira.mariadb.org/browse/MXS-4280) qc_sqlite does not properly handle a LIMIT clause
* [MXS-4279](https://jira.mariadb.org/browse/MXS-4279) "sub" field not set for JWTs
* [MXS-4278](https://jira.mariadb.org/browse/MXS-4278) The current connection name in the dropdown disappears when failed to connect to a new connection
* [MXS-4253](https://jira.mariadb.org/browse/MXS-4253) Redirecting to 404 page after login with AD PAM user
* [MXS-4251](https://jira.mariadb.org/browse/MXS-4251) Stop button is not working properly and high memory usage in the Query Editor
* [MXS-4241](https://jira.mariadb.org/browse/MXS-4241) MaxScale High CPU / Load issue
* [MXS-4174](https://jira.mariadb.org/browse/MXS-4174) expire_log_duration and expire_log_minimum_files not covered by tests
* [MXS-4156](https://jira.mariadb.org/browse/MXS-4156) Update documentation on required monitor privileges
* [MXS-4083](https://jira.mariadb.org/browse/MXS-4083) CPU utilization high on MaxScale host

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
