# MariaDB MaxScale 2.2.17 Release Notes

Release 2.2.17 is a GA release.

This document describes the changes in release 2.2.17, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-2175](https://jira.mariadb.org/browse/MXS-2175) available_when_donor=true is not respected in Galera Monitor with xtrabackup-v2 sst method and only one synced node available in the cluster
* [MXS-2159](https://jira.mariadb.org/browse/MXS-2159) Oracle Connector/J 8.0 with SSL doesn't work with MaxScale
* [MXS-2151](https://jira.mariadb.org/browse/MXS-2151) MaxScale does not log any info when "Connection killed by MaxScale: Router could not recover from connection errors"
* [MXS-2106](https://jira.mariadb.org/browse/MXS-2106) Maxscale CDC JSON output does not respect null values
* [MXS-2095](https://jira.mariadb.org/browse/MXS-2095) Fatal: MaxScale 2.2.15 received fatal signal 11 - libavrorouter.so(count_columns+0x21)
* [MXS-2081](https://jira.mariadb.org/browse/MXS-2081) Unable to execute maxctrl commands in RHEL 6 - Maxscale 2.2.9

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
