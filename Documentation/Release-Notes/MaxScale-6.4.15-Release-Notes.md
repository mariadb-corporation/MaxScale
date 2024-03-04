# MariaDB MaxScale 6.4.15 Release Notes -- 2024-03-11

Release 6.4.15 is a GA release.

This document describes the changes in release 6.4.15, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-6.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-5007](https://jira.mariadb.org/browse/MXS-5007) Top-level service reconnection may cause a use-after-free
* [MXS-5000](https://jira.mariadb.org/browse/MXS-5000) insertstream uses an error code from the reserved client range
* [MXS-4998](https://jira.mariadb.org/browse/MXS-4998) MaxScale may send two COM_QUIT packets
* [MXS-4997](https://jira.mariadb.org/browse/MXS-4997) MaxScale: BUILD/install_build_deps.sh: deprecated --force-yes
* [MXS-4981](https://jira.mariadb.org/browse/MXS-4981) Hang on shutdown when large batches of session command are pending
* [MXS-4979](https://jira.mariadb.org/browse/MXS-4979) COM_CHANGE_USER may leave stale IDs to be checked
* [MXS-4978](https://jira.mariadb.org/browse/MXS-4978) Read-only transactions are incorrectly tracked
* [MXS-4967](https://jira.mariadb.org/browse/MXS-4967) Log throttling is sometimes disabled too early
* [MXS-4943](https://jira.mariadb.org/browse/MXS-4943) delayed_retry timeout errors do not have enough information

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
