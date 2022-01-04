# MariaDB MaxScale 2.4.19 Release Notes

Release 2.4.19 is a GA release.

This document describes the changes in release 2.4.19, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-2.4.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3892](https://jira.mariadb.org/browse/MXS-3892) schema router flood information schemas with queries
* [MXS-3885](https://jira.mariadb.org/browse/MXS-3885) MaxScale unconditionally loads global options from /etc/maxscale.cnf.d/maxscale.cnf
* [MXS-3879](https://jira.mariadb.org/browse/MXS-3879) MaxScale doesn't load maxscale section in persistent file after restart
* [MXS-3873](https://jira.mariadb.org/browse/MXS-3873) Crash in qc_sqlite
* [MXS-3617](https://jira.mariadb.org/browse/MXS-3617) writeq throttling can lose response packets
* [MXS-3585](https://jira.mariadb.org/browse/MXS-3585) query classifier crashes after upgrade from 2.5.11 to 2.5.12
* [MXS-3310](https://jira.mariadb.org/browse/MXS-3310) Amend documentation on keytab file location to mention the possibility to use the environment variable KRB5_KTNAME

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
