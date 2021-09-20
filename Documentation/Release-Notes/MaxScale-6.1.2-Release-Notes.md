# MariaDB MaxScale 6.1.2 Release Notes

Release 6.1.2 is a GA release.

This document describes the changes in release 6.1.2, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-6.1.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-3773](https://jira.mariadb.org/browse/MXS-3773) nosqlprotocol should report 0 as the minimum wire protocol version
* [MXS-3766](https://jira.mariadb.org/browse/MXS-3766) Not able to insert data on Masking enabled table 
* [MXS-3759](https://jira.mariadb.org/browse/MXS-3759) Client hangs forever when server failed or restarted
* [MXS-3757](https://jira.mariadb.org/browse/MXS-3757) Don't allow any SIMD code run until called
* [MXS-3704](https://jira.mariadb.org/browse/MXS-3704) MaxScale always advertises the SESSION_TRACK capability even with servers that don't support it (XPand)
* [MXS-3580](https://jira.mariadb.org/browse/MXS-3580) Avrorouter should store full GTID coordinates
* [MXS-3514](https://jira.mariadb.org/browse/MXS-3514) Speed up special query parsing (pcre2)

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
