# MariaDB MaxScale 23.08 Release Notes -- 2023-08-

Release 23.08.0 is a Beta release.

This document describes the changes in release 23.08, when compared to
release 23.02.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Changed Features

###

## Dropped Features

###

## Deprecated Features

   * The configuration parameters `query_classifier` and `query_classifier_args`
     have been deprecated and are ignored.

## New Features

### [MXS-4232](https://jira.mariadb.org/browse/MXS-4232) Remember old service password

When the service password is changed, MaxScale will remember and use the previous
password if the new does not work. This makes it easier to manage the changing of
the password, as the password in the backend and in MaxScale need not be changed
simultaneously. More information about this functionality can be found
[here](../Getting-Started/Configuration-Guide.md#user-and-password).

### [MXS-4377](https://jira.mariadb.org/browse/MXS-4377) Common options

It is now possible to specify options in an _include_-section, to be included
by other sections. This is useful, for instance, if there are multiple monitors
that otherwise are identically configured, but for their list of servers. More
information about this functionality can be found
[here](../Getting-Started/Configuration-Guide.md#include-1).

### [MXS-4549](https://jira.mariadb.org/browse/MXS-4549) Replay queries with partially returned results

If a query in a transaction is interrupted and the result was partially
delivered to the client, readwritesplit will now retry the execution of the
query and discard the already delivered part of the result.

### MaxGUI


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
