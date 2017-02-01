
# MariaDB MaxScale 1.4.5 Release Notes -- 2017-02-01

Release 1.4.5 is a GA release.

This document describes the changes in release 1.4.5, when compared to
release 1.4.4.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

- The dbfwfilter now rejects all prepared statements instead of ignoring
  them. This affects _wildcard_, _columns_, _on_queries_ and _no_where_clause_
  type rules which previously ignored prepared statements.

## Bug fixes

[Here is a list of bugs fixed since the release of MaxScale 1.4.4.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20resolution%20in%20(Fixed%2C%20Done)%20AND%20fixVersion%20%3D%201.4.5)

* [MXS-1082](https://jira.mariadb.org/browse/MXS-1082): Block prepared statements

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the Linux distributions supported
by MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/resources/downloads).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is derived
from the version of MaxScale. For instance, the tag of version `X.Y.Z` of MaxScale
is `maxscale-X.Y.Z`.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
