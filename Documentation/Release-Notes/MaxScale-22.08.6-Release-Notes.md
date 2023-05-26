# MariaDB MaxScale 22.08.6 Release Notes

Release 22.08.6 is a GA release.

This document describes the changes in release 22.08.6, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-22.08.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-4625](https://jira.mariadb.org/browse/MXS-4625) Query classifier does not classify XA transactions correctly.
* [MXS-4615](https://jira.mariadb.org/browse/MXS-4615) Partially executed multi-result queries are not treated as partial results
* [MXS-4614](https://jira.mariadb.org/browse/MXS-4614) Query classifier does not recognize BEGIN NOT ATOMIC ... END
* [MXS-4612](https://jira.mariadb.org/browse/MXS-4612) Query Editor: High memory usage when multiple statements are executed in a batch query
* [MXS-4611](https://jira.mariadb.org/browse/MXS-4611) Readwritesplit prefers idle primary over busy replicas
* [MXS-4602](https://jira.mariadb.org/browse/MXS-4602) Qlafilter logs responses from non-matched queries
* [MXS-4599](https://jira.mariadb.org/browse/MXS-4599) AVX instructions end up being executed on startup
* [MXS-4596](https://jira.mariadb.org/browse/MXS-4596) Query canonicalization does not work on scientific numbers
* [MXS-4595](https://jira.mariadb.org/browse/MXS-4595) maxctrl classify sends malformed SQL
* [MXS-4586](https://jira.mariadb.org/browse/MXS-4586) transaction_replay_max_size default is 1GiB instead of 1MiB
* [MXS-4571](https://jira.mariadb.org/browse/MXS-4571) Passwords appear masked even if they are not set
* [MXS-4570](https://jira.mariadb.org/browse/MXS-4570) CREATE USER statements aren't obfuscated in the Query Editor history query
* [MXS-4569](https://jira.mariadb.org/browse/MXS-4569) Undefined behavior in simd_canonical.cc
* [MXS-4560](https://jira.mariadb.org/browse/MXS-4560) Not all passwords were obfuscated in the maxctrl report
* [MXS-4550](https://jira.mariadb.org/browse/MXS-4550) Regular expression documentation is inaccurate and lacking
* [MXS-4548](https://jira.mariadb.org/browse/MXS-4548) The statement canonicalizer cannot handle comments within statements
* [MXS-4502](https://jira.mariadb.org/browse/MXS-4502) KB pages reference mysqlauth and mysqlauth is deprecated for mariadbauth

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
