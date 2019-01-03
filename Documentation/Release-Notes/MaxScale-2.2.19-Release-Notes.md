# MariaDB MaxScale 2.2.19 Release Notes

Release 2.2.19 is a GA release.

This document describes the changes in release 2.2.19, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-2238](https://jira.mariadb.org/browse/MXS-2238) MaxScale fails to send large CDC schemas
* [MXS-2234](https://jira.mariadb.org/browse/MXS-2234) Add extra info to log when MaxScale loads persisted configuration files
* [MXS-2232](https://jira.mariadb.org/browse/MXS-2232) version_string prefix 5.5.5- is always added
* [MXS-2231](https://jira.mariadb.org/browse/MXS-2231) Kerberos together with ssl doesn't work

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
