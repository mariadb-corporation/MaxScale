# MariaDB MaxScale 2.2.10 Release Notes -- 2018-06-28

Release 2.2.10 is a GA release.

This document describes the changes in release 2.2.10, when compared to
release 2.2.9.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Bug fixes

* [MXS-1935](https://jira.mariadb.org/browse/MXS-1935) PREPARE ... FROM @var is not parsed
* [MXS-1932](https://jira.mariadb.org/browse/MXS-1932) MaxScale reads hidden files from maxscale.cnf.d
* [MXS-1931](https://jira.mariadb.org/browse/MXS-1931) Debug assertion in is_large_query fails
* [MXS-1930](https://jira.mariadb.org/browse/MXS-1930) New capability flags aren't used with MariaDB 10.3
* [MXS-1926](https://jira.mariadb.org/browse/MXS-1926) LOAD DATA LOCAL INFILE interrupted by slave shutdown
* [MXS-1920](https://jira.mariadb.org/browse/MXS-1920)  # is not recognized as an until-end-of-line comment character
* [MXS-1913](https://jira.mariadb.org/browse/MXS-1913) fatal signal 11
* [MXS-1911](https://jira.mariadb.org/browse/MXS-1911) Certificate verification cannot be disabled for created listeners
* [MXS-1910](https://jira.mariadb.org/browse/MXS-1910) Only ssl_ca_cert should be required for servers
* [MXS-1907](https://jira.mariadb.org/browse/MXS-1907) Can't define ssl_verify_peer_certificate at runtime
* [MXS-1902](https://jira.mariadb.org/browse/MXS-1902) COM_CHANGE_USER lost connection
* [MXS-1891](https://jira.mariadb.org/browse/MXS-1891) DEALLOCATE PREPARE should route to all
* [MXS-1887](https://jira.mariadb.org/browse/MXS-1887) Using cache causes slow read from mysql server
* [MXS-1749](https://jira.mariadb.org/browse/MXS-1749) Process datadir is not always deleted on exit
* [MXS-872](https://jira.mariadb.org/browse/MXS-872) MaxScale doesn't understand roles

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the Linux distributions supported
by MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/downloads/mariadb-tx/maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is X.Y.Z. Further, *master* always refers to the latest released non-beta version.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
