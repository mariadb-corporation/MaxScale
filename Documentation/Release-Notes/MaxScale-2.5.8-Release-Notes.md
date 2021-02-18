# MariaDB MaxScale 2.5.8 Release Notes -- 2021-02-18

Release 2.5.8 is a GA release.

This document describes the changes in release 2.5.8, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-2.5.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3408](https://jira.mariadb.org/browse/MXS-3408) ASAN reports leaks in the query classifier
* [MXS-3404](https://jira.mariadb.org/browse/MXS-3404) maxscale write in the slave with function
* [MXS-3403](https://jira.mariadb.org/browse/MXS-3403) The Cache Filter cannot deal with batched requests
* [MXS-3400](https://jira.mariadb.org/browse/MXS-3400) maxscale crash with memcache on ubuntu 20
* [MXS-3399](https://jira.mariadb.org/browse/MXS-3399) QC heap-buffer-overflow
* [MXS-3396](https://jira.mariadb.org/browse/MXS-3396) Default /etc/maxscale.cnf for MaxScale 2.5 comments direct user to MaxScale 2.4 documents
* [MXS-3395](https://jira.mariadb.org/browse/MXS-3395) Problems with prepared statements with more than 2^16 columns
* [MXS-3380](https://jira.mariadb.org/browse/MXS-3380) MaxScale crash loop with cache filter + Redis
* [MXS-3366](https://jira.mariadb.org/browse/MXS-3366) COM_CHANGE_USER rejected by MaxScale

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
