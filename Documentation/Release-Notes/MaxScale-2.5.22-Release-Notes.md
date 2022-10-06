# MariaDB MaxScale 2.5.22 Release Notes -- 2022-10-06

Release 2.5.22 is a GA release.

This document describes the changes in release 2.5.22, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-2.5.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-4332](https://jira.mariadb.org/browse/MXS-4332) REST API reports unknown parameters with warnings and not errors
* [MXS-4331](https://jira.mariadb.org/browse/MXS-4331) Altering servers fails if SSL is enabled
* [MXS-4321](https://jira.mariadb.org/browse/MXS-4321) Error from missing --secure option is not helpful
* [MXS-4313](https://jira.mariadb.org/browse/MXS-4313) MaxCtrl misinterprets some arguments
* [MXS-4312](https://jira.mariadb.org/browse/MXS-4312) REST API accepts empty resource IDs
* [MXS-4304](https://jira.mariadb.org/browse/MXS-4304) MariaDB-Monitor spams log with connection errors if server is both [Maintenance] and [Down]
* [MXS-4283](https://jira.mariadb.org/browse/MXS-4283) Race condition in KILL command processing
* [MXS-4279](https://jira.mariadb.org/browse/MXS-4279) "sub" field not set for JWTs
* [MXS-4269](https://jira.mariadb.org/browse/MXS-4269) UPDATE with user variable modification is treated as a session command
* [MXS-4240](https://jira.mariadb.org/browse/MXS-4240) MXS-4239 readconnroute module routing read queries to inconsistent slave node 
* [MXS-4239](https://jira.mariadb.org/browse/MXS-4239) Maxscale shows replication  status  as [Slave, Running] even when replication credentials are wrong
* [MXS-4227](https://jira.mariadb.org/browse/MXS-4227) MaxCtrl incompatibility with MemoryDenyWriteExecute=true is not documented
* [MXS-4224](https://jira.mariadb.org/browse/MXS-4224) connection_timeout not documented to only take effect on the top level service
* [MXS-4209](https://jira.mariadb.org/browse/MXS-4209) KILL command doesn't work correctly if persistent connections are enabled
* [MXS-4198](https://jira.mariadb.org/browse/MXS-4198) MaxScale fails to validate its own certificate when the chain of trust is unknown to OpenSSL
* [MXS-4196](https://jira.mariadb.org/browse/MXS-4196) Readconnroute load balancing behavior is not well documented
* [MXS-4156](https://jira.mariadb.org/browse/MXS-4156) Update documentation on required monitor privileges
* [MXS-4148](https://jira.mariadb.org/browse/MXS-4148) Log warning if reverse name resolution takes significant time
* [MXS-4094](https://jira.mariadb.org/browse/MXS-4094) Allow empty token when client is replying to AuthSwitchRequest

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
