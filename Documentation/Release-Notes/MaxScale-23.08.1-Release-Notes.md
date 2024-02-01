# MariaDB MaxScale 23.08.1 Release Notes -- 2023-09-21

Release 23.08.1 is a GA release.

This document describes the changes in release 23.08.1, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-23.08.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## External CVEs resolved.

* [CVE-2023-27371](https://www.cve.org/CVERecord?id=CVE-2023-27371) Fixed by [MXS-4751](https://jira.mariadb.org/browse/MXS-4751) Update libmicrohttpd to version 0.9.76

## New Features

* [MXS-3761](https://jira.mariadb.org/browse/MXS-3761) Visualize `response_time_distribution`
* [MXS-3753](https://jira.mariadb.org/browse/MXS-3753) Add option to run PAM authentication in a suid sandbox

## Bug fixes

* [MXS-4762](https://jira.mariadb.org/browse/MXS-4762) REST-API generates too many errors for some endpoints
* [MXS-4760](https://jira.mariadb.org/browse/MXS-4760) Automatically ignored tables are not documented for schemarouter
* [MXS-4749](https://jira.mariadb.org/browse/MXS-4749) log_throttling should be disabled if log_info is on
* [MXS-4735](https://jira.mariadb.org/browse/MXS-4735) Connection IDs are missing from error messages
* [MXS-4724](https://jira.mariadb.org/browse/MXS-4724) slave_selection_criteria should accept lowercase version of the values
* [MXS-4723](https://jira.mariadb.org/browse/MXS-4723) Passthrough authentication does not support COM_CHANGE_USER

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
