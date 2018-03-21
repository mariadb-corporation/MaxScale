# MariaDB MaxScale 2.2.4 Release Notes -- 2018-03

Release 2.2.4 is a GA release.

This document describes the changes in release 2.2.4, when compared to
release 2.2.3.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

### Masking is stricter

If a masking rule specifies the table/database in addition to the column
name, then if a resultset does not contain table/database information, it
is considered a match if the column name matches. Please consult the
[documentation](../Filters/Masking.md) for details.

## Dropped Features

## New Features

## Bug fixes

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the Linux distributions supported
by MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/resources/downloads).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is X.Y.Z. Further, *master* always refers to the latest released non-beta version.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
