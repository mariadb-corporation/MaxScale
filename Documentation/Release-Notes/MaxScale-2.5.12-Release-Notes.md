# MariaDB MaxScale 2.5.12 Release Notes -- 2021-05-26

**NOTE** After 2.5.12 was released, a serious
[regression](https://jira.mariadb.org/browse/MXS-3585)
was noticed. Do not use MaxScale 2.5.12, but install or upgrade to
MaxScale 2.5.13 that contains a fix for the regression.

Release 2.5.12 is a GA release.

This document describes the changes in release 2.5.12, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-2.5.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3566](https://jira.mariadb.org/browse/MXS-3566) EXPLAIN leaks memory
* [MXS-3565](https://jira.mariadb.org/browse/MXS-3565) COM_STMT_EXECUTE target selection is too restrictive when no metadata is provided
* [MXS-3548](https://jira.mariadb.org/browse/MXS-3548) Plugin name comparison is case-sensitive
* [MXS-3538](https://jira.mariadb.org/browse/MXS-3538) Removal of authenticator_options is not documented in upgrade documents
* [MXS-3536](https://jira.mariadb.org/browse/MXS-3536) max_slave_connections=0 doesn't work as expected
* [MXS-3535](https://jira.mariadb.org/browse/MXS-3535) user variable is not collected if it's in join clause
* [MXS-3533](https://jira.mariadb.org/browse/MXS-3533) MaxScale doesn't advertise the SESSION_TRACK capability
* [MXS-3529](https://jira.mariadb.org/browse/MXS-3529) Maxscale is not compatible with the latest cmake 3.20
* [MXS-3528](https://jira.mariadb.org/browse/MXS-3528) Maxscale source repo contains old boost-1.73.0.tar.gz which is not buildable with latest cmake on ARM
* [MXS-3524](https://jira.mariadb.org/browse/MXS-3524) alter syslog  is not supported during runtime 
* [MXS-3479](https://jira.mariadb.org/browse/MXS-3479) Disabling maxlog doesn't fully prevent it from being written to

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
