# MariaDB MaxScale 2.4.16 Release Notes

Release 2.4.16 is a GA release.

This document describes the changes in release 2.4.16, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-2.4.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3408](https://jira.mariadb.org/browse/MXS-3408) ASAN reports leaks in the query classifier
* [MXS-3404](https://jira.mariadb.org/browse/MXS-3404) maxscale write in the slave with function
* [MXS-3399](https://jira.mariadb.org/browse/MXS-3399) QC heap-buffer-overflow
* [MXS-3345](https://jira.mariadb.org/browse/MXS-3345) --basedir overrides more specific settings like --connector_plugindir
* [MXS-3330](https://jira.mariadb.org/browse/MXS-3330) Fatal: MaxScale 2.4.14 received fatal signal 11

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
