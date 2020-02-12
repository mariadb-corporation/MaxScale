# MariaDB MaxScale 2.4.7 Release Notes

Release 2.4.7 is a GA release.

This document describes the changes in release 2.4.7, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-2889](https://jira.mariadb.org/browse/MXS-2889) Direct replication mode can hang on shutdown
* [MXS-2887](https://jira.mariadb.org/browse/MXS-2887) admin_auth documented as not modifiable at runtime
* [MXS-2883](https://jira.mariadb.org/browse/MXS-2883) session closed by maxscale when it received "auth switch request" packet from backend server
* [MXS-2880](https://jira.mariadb.org/browse/MXS-2880) Typo in MariaDBMonitor slave connection printing
* [MXS-2878](https://jira.mariadb.org/browse/MXS-2878) Monitor connections do not insist on SSL being used
* [MXS-2871](https://jira.mariadb.org/browse/MXS-2871) maxscale RPM post-uninstall script has bugs
* [MXS-2859](https://jira.mariadb.org/browse/MXS-2859) Strings with newlines break configuration serialization 
* [MXS-2858](https://jira.mariadb.org/browse/MXS-2858) Hang with writeq_high_water
* [MXS-2857](https://jira.mariadb.org/browse/MXS-2857) ssl_verify_peer_certificate should not be on by default
* [MXS-2851](https://jira.mariadb.org/browse/MXS-2851) CAST Function displays contents of Masked column
* [MXS-2850](https://jira.mariadb.org/browse/MXS-2850) MaxScale masking does not work with UNION ALL
* [MXS-2822](https://jira.mariadb.org/browse/MXS-2822) Unexpected internal state: received response
* [MXS-2810](https://jira.mariadb.org/browse/MXS-2810) maxscale process still running after uninstalling maxscale package
* [MXS-2784](https://jira.mariadb.org/browse/MXS-2784) Default charset is not correct

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
