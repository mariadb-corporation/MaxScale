# MariaDB MaxScale 24.08.0 Release Notes --

Release 24.08.0 is a Beta release.

This document describes the changes in release 24.08, when compared to
release 24.02.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Changed Features

### [MXS-5122](https://jira.mariadb.org/browse/MXS-5122) Scale everything according to the amount of available container resources

If MaxScale is running in a container, it will adapt to amount of resources (CPUs and
memory) available in the container. See [threads](../Getting-Started/Configuration-Guide.md#threads)
and [query_classifier_cache_size](../Getting-Started/Configuration-Guide.md#query_classifier_cache_size)
for more information.

## Dropped Features

## Deprecated Features

## New Features

### MaxGUI
Numerous additions have been added and improvements made to MaxGUI.
The most notable ones are listed here:

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
