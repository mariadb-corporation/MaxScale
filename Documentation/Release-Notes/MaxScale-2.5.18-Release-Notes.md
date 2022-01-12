# MariaDB MaxScale 2.5.18 Release Notes -- 2022-01-12

Release 2.5.18 is a GA release.

This document describes the changes in release 2.5.18, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-2.5.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3938](https://jira.mariadb.org/browse/MXS-3938) Debug assert in xpandmon
* [MXS-3934](https://jira.mariadb.org/browse/MXS-3934) Linking a service at runtime to an xpandmon doesn't work
* [MXS-3933](https://jira.mariadb.org/browse/MXS-3933) Avro reader client can fail
* [MXS-3928](https://jira.mariadb.org/browse/MXS-3928) MaxScale logs a warning when users are loaded from a Xpand cluster
* [MXS-3920](https://jira.mariadb.org/browse/MXS-3920) Can't connect to MaxScale when schema uses utf8mb4 chars >= U0080
* [MXS-3897](https://jira.mariadb.org/browse/MXS-3897) MaxScale crashes when executing CDC process to kafka

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
