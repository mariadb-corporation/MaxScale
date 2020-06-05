# MariaDB MaxScale 2.3.20 Release Notes -- 2020-06-05

Release 2.3.20 is a GA release.

This document describes the changes in release 2.3.20, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3016](https://jira.mariadb.org/browse/MXS-3016) `create server` can accept multiple monitors
* [MXS-3014](https://jira.mariadb.org/browse/MXS-3014) Missing parameters in /maxscale endpoint
* [MXS-2998](https://jira.mariadb.org/browse/MXS-2998) maxctrl parsing trouble
* [MXS-2990](https://jira.mariadb.org/browse/MXS-2990) QC bug
* [MXS-2984](https://jira.mariadb.org/browse/MXS-2984) maxctrl list listeners output is not correct
* [MXS-2983](https://jira.mariadb.org/browse/MXS-2983) --servers option doesn't document value syntax
* [MXS-2982](https://jira.mariadb.org/browse/MXS-2982) maxscale --help points to old documentation
* [MXS-2981](https://jira.mariadb.org/browse/MXS-2981) Missng REST API TLS certificates are not a hard error
* [MXS-2980](https://jira.mariadb.org/browse/MXS-2980) maxctrl not using SSL/TLS in interactive mode

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
