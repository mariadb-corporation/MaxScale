# MariaDB MaxScale 2.3.16 Release Notes -- 2020-01-16

Release 2.3.16 is a GA release.

This document describes the changes in release 2.3.16, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-2758](https://jira.mariadb.org/browse/MXS-2758) Enable the systemd unit after setting up the maxscale package

## Bug fixes

* [MXS-2829](https://jira.mariadb.org/browse/MXS-2829) Deleting a filter doesn't remove the persisted file
* [MXS-2825](https://jira.mariadb.org/browse/MXS-2825) REST API allows POST requests without body for basic users
* [MXS-2824](https://jira.mariadb.org/browse/MXS-2824) Basic user access is not documented from maxctrl's perspective
* [MXS-2820](https://jira.mariadb.org/browse/MXS-2820) Wrong error message for failed authentication
* [MXS-2813](https://jira.mariadb.org/browse/MXS-2813) maxctl shows password in clear text
* [MXS-2810](https://jira.mariadb.org/browse/MXS-2810) maxscale process still running after uninstalling maxscale package
* [MXS-2797](https://jira.mariadb.org/browse/MXS-2797) maxscale not started with handle_server_events
* [MXS-2792](https://jira.mariadb.org/browse/MXS-2792) Documentation for MaxScale's monitor script is incomplete
* [MXS-2789](https://jira.mariadb.org/browse/MXS-2789) Stale journal messages are warnings
* [MXS-2788](https://jira.mariadb.org/browse/MXS-2788) Masking filter performs case-sensitive checks against unquoted case-insensitive identifiers in function calls and WHERE clauses
* [MXS-2759](https://jira.mariadb.org/browse/MXS-2759) Nontrivial number of privilege entries causes long running queries / overload
* [MXS-2710](https://jira.mariadb.org/browse/MXS-2710) max_connections problem
* [MXS-619](https://jira.mariadb.org/browse/MXS-619) creating many short sessions in parallel leads to errors

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
