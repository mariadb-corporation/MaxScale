# MariaDB MaxScale 2.5.14 Release Notes -- 2021-07-21

Release 2.5.14 is a GA release.

This document describes the changes in release 2.5.14, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-2.5.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3650](https://jira.mariadb.org/browse/MXS-3650) Maxscale crashes while logging to GUI
* [MXS-3623](https://jira.mariadb.org/browse/MXS-3623) Race condition in persistent connections
* [MXS-3622](https://jira.mariadb.org/browse/MXS-3622) Backend connection isn't closed right after authentication failure
* [MXS-3617](https://jira.mariadb.org/browse/MXS-3617) writeq throttling can lose response packets
* [MXS-3615](https://jira.mariadb.org/browse/MXS-3615) Clarify session_track_system_variables requirement for readwritesplit's causal_reads parameter
* [MXS-3595](https://jira.mariadb.org/browse/MXS-3595) Listener subresource returns wrong response on service-listener mismatch
* [MXS-3590](https://jira.mariadb.org/browse/MXS-3590) MaxCtrl documentation for the reload command is broken
* [MXS-3588](https://jira.mariadb.org/browse/MXS-3588) BULK pipelining error
* [MXS-3578](https://jira.mariadb.org/browse/MXS-3578) Unexpected result state when using connection_keepalive
* [MXS-3574](https://jira.mariadb.org/browse/MXS-3574) Output examples in the REST API documentation are out of date
* [MXS-2890](https://jira.mariadb.org/browse/MXS-2890) 5.5.5 prefix is always added even with version_string=8.0.16

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
