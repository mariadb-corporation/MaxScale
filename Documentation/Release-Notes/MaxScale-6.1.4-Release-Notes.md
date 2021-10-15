# MariaDB MaxScale 6.1.4 Release Notes

Release 6.1.4 is a GA release.

This document describes the changes in release 6.1.4, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-6.1.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3817](https://jira.mariadb.org/browse/MXS-3817) The location of the GUI web directory isn't documented
* [MXS-3816](https://jira.mariadb.org/browse/MXS-3816) Queries are not always counted as reads with router_options=slave
* [MXS-3812](https://jira.mariadb.org/browse/MXS-3812) Hints for prepared statements can be lost if a query fails
* [MXS-3804](https://jira.mariadb.org/browse/MXS-3804) Result size accounting is wrong
* [MXS-3803](https://jira.mariadb.org/browse/MXS-3803) Debug assertion in readwritesplit
* [MXS-3801](https://jira.mariadb.org/browse/MXS-3801) Unexpected internal state with read-only cursor and result with one row
* [MXS-3799](https://jira.mariadb.org/browse/MXS-3799) Destroyed monitors are not deleted
* [MXS-3798](https://jira.mariadb.org/browse/MXS-3798) Race condition in service destruction
* [MXS-3791](https://jira.mariadb.org/browse/MXS-3791) Fix generix multistatement bug
* [MXS-3790](https://jira.mariadb.org/browse/MXS-3790) Fix luafilter
* [MXS-3788](https://jira.mariadb.org/browse/MXS-3788) Debug assertion with default config and transaction_replay=true
* [MXS-3779](https://jira.mariadb.org/browse/MXS-3779) binlogrouter logs warnings for ignored SQL
* [MXS-3768](https://jira.mariadb.org/browse/MXS-3768) Query Editor requires admin privileges

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
