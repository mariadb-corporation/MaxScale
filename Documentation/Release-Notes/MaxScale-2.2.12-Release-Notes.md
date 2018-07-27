# MariaDB MaxScale 2.2.12 Release Notes -- 2018-7-27

Release 2.2.12 is a GA release.

This document describes the changes in release 2.2.12, when compared to
release 2.2.11.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

### Configuration Exporting

The runtime configuration can now be dumped into a file with the
`--export-config` command line option. This allows changes done at runtime to be
collected into a single file for easier exporting.

## Bug fixes

* [MXS-1985](https://jira.mariadb.org/browse/MXS-1985) Concurrent KILL commands cause deadlock
* [MXS-1977](https://jira.mariadb.org/browse/MXS-1977) Maxscale 2.2.6 memory leak
* [MXS-1949](https://jira.mariadb.org/browse/MXS-1949) Warning for user load failure logged even when service has no users
* [MXS-1942](https://jira.mariadb.org/browse/MXS-1942) maxctrl --version is not helpful

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for supported the Linux distributions.

Packages can be downloaded [here](https://mariadb.com/downloads/mariadb-tx/maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is `maxscale-X.Y.Z`. Further, the default branch is always the latest GA version
of MaxScale.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
