# MariaDB MaxScale 6.3.0 Release Notes

Release 6.3.0 is a GA release.

This document describes the changes in release 6.3.0, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-6.3.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-3968](https://jira.mariadb.org/browse/MXS-3968) Add support for SSL
* [MXS-3925](https://jira.mariadb.org/browse/MXS-3925) Implement authentication
* [MXS-3902](https://jira.mariadb.org/browse/MXS-3902) Limit total number of connections to backend
* [MXS-3844](https://jira.mariadb.org/browse/MXS-3844) Cooperative Monitor Indicator
* [MXS-3806](https://jira.mariadb.org/browse/MXS-3806) Provide filtering for the KafkaCDC Router
* [MXS-3413](https://jira.mariadb.org/browse/MXS-3413) The persistence of on-the-fly parameter changes needs to be somehow exposed, and more manageable.

## Bug fixes

* [MXS-4082](https://jira.mariadb.org/browse/MXS-4082) SQL endpoint doesn't show errors for resultsets
* [MXS-4080](https://jira.mariadb.org/browse/MXS-4080) Query Cache detects wrong parse error in INSERT or DELETE
* [MXS-4078](https://jira.mariadb.org/browse/MXS-4078) maxctrl commands exception with file .maxctrl.cnf
* [MXS-4074](https://jira.mariadb.org/browse/MXS-4074) Status of boostrap servers not always the same as the status of corresponding runtime servers
* [MXS-4071](https://jira.mariadb.org/browse/MXS-4071) A horizontal scrollbar appears in some dialogs
* [MXS-4064](https://jira.mariadb.org/browse/MXS-4064) Address field truncated in GUI
* [MXS-4053](https://jira.mariadb.org/browse/MXS-4053) The cache does not handle multi-statements properly.
* [MXS-4027](https://jira.mariadb.org/browse/MXS-4027) Query Editor Chart is Not Hiding Or need close button For the Chart
* [MXS-3977](https://jira.mariadb.org/browse/MXS-3977) The servers table in monitor details page shouldn't be sorted by default
* [MXS-3962](https://jira.mariadb.org/browse/MXS-3962) Automatically generated dynamic config contains default values for unmodified params

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
