# MariaDB MaxScale 25.02.0 Release Notes --

Release 25.02.0 is a Beta release.

This document describes the changes in release 25.02, when compared to
release 24.08.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Changed Features

## Dropped Features

## Deprecated Features

## New Features

### [MXS-5149](https://jira.mariadb.org/browse/MXS-5149) Include actual line content in parse error message

When configuration file parsing fails in an early phase, the actual offending
line will now be logged in the error message to make it easier to pinpoint
the problem.

### [MXS-5177](https://jira.mariadb.org/browse/MXS-5177) Introduce a new core_file variable in MaxScale

By default, MaxScale no longer generates a core file when it crashes.
Please see [core_file](../Getting-Started/Configuration-Guide.md#core_file)
for more information.

## Bug fixes

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
