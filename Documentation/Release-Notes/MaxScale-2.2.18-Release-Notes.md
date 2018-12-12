# MariaDB MaxScale 2.2.18 Release Notes

Release 2.2.18 is a GA release.

This document describes the changes in release 2.2.18, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-2221](https://jira.mariadb.org/browse/MXS-2221) Fatal signal handling does not always create a core
* [MXS-2216](https://jira.mariadb.org/browse/MXS-2216) Read past stack buffer
* [MXS-2213](https://jira.mariadb.org/browse/MXS-2213) Growing memory consumption with 2.2.17
* [MXS-2207](https://jira.mariadb.org/browse/MXS-2207) qc_mysqlembedded does not classify SET STATEMENT ... FOR UPDATE correctly.
* [MXS-2183](https://jira.mariadb.org/browse/MXS-2183) Excessive memory use, but not growing endlessly

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
