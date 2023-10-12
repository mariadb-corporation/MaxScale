# MariaDB MaxScale 2.5.29 Release Notes

Release 2.5.29 is a GA release.

This document describes the changes in release 2.5.29, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-2.5.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## External CVEs resolved.

* [CVE-2022-1586](https://www.cve.org/CVERecord?id=CVE-2022-1586) Fixed by [MXS-4806](https://jira.mariadb.org/browse/MXS-4806) Update pcre2 to 10.42
* [CVE-2022-1587](https://www.cve.org/CVERecord?id=CVE-2022-1587) Fixed by [MXS-4806](https://jira.mariadb.org/browse/MXS-4806) Update pcre2 to 10.42
* [CVE-2022-41409](https://www.cve.org/CVERecord?id=CVE-2022-41409) Fixed by [MXS-4806](https://jira.mariadb.org/browse/MXS-4806) Update pcre2 to 10.42
* [CVE-2017-7186](https://www.cve.org/CVERecord?id=CVE-2017-7186) Fixed by [MXS-4804](https://jira.mariadb.org/browse/MXS-4804) Update pcre2 to 10.36
* [CVE-2017-8399](https://www.cve.org/CVERecord?id=CVE-2017-8399) Fixed by [MXS-4804](https://jira.mariadb.org/browse/MXS-4804) Update pcre2 to 10.36
* [CVE-2017-8786](https://www.cve.org/CVERecord?id=CVE-2017-8786) Fixed by [MXS-4804](https://jira.mariadb.org/browse/MXS-4804) Update pcre2 to 10.36
* [CVE-2020-7105](https://www.cve.org/CVERecord?id=CVE-2020-7105) Fixed by [MXS-4757](https://jira.mariadb.org/browse/MXS-4757) Update libhiredis to 1.0.2.
* [CVE-2023-27371](https://www.cve.org/CVERecord?id=CVE-2023-27371) Fixed by [MXS-4751](https://jira.mariadb.org/browse/MXS-4751) Update libmicrohttpd to version 0.9.76

## Bug fixes

* [MXS-4807](https://jira.mariadb.org/browse/MXS-4807) MaxScale does not always report the OS version correctly
* [MXS-4756](https://jira.mariadb.org/browse/MXS-4756) GUI caching issue
* [MXS-4749](https://jira.mariadb.org/browse/MXS-4749) log_throttling should be disabled if log_info is on
* [MXS-4747](https://jira.mariadb.org/browse/MXS-4747) log_throttling is hard to modify via MaxCtrl
* [MXS-4738](https://jira.mariadb.org/browse/MXS-4738) The fact that disable_master_failback does not work with root_node_as_master is not documented
* [MXS-4735](https://jira.mariadb.org/browse/MXS-4735) Connection IDs are missing from error messages
* [MXS-4724](https://jira.mariadb.org/browse/MXS-4724) slave_selection_criteria should accept lowercase version of the values
* [MXS-4717](https://jira.mariadb.org/browse/MXS-4717) information_schema is not invalidated as needed
* [MXS-4706](https://jira.mariadb.org/browse/MXS-4706) Cache does not invalidate when a table is ALTERed, DROPed or RENAMEd

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
