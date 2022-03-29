# MariaDB MaxScale 6.2.4 Release Notes

Release 6.2.4 is a GA release.

This document describes the changes in release 6.2.4, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-6.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-3997](https://jira.mariadb.org/browse/MXS-3997) Name threads for better CPU usage view
* [MXS-3665](https://jira.mariadb.org/browse/MXS-3665) Provide more feedback on TLS cipher  mismatch

## Bug fixes

* [MXS-4061](https://jira.mariadb.org/browse/MXS-4061) Query Editor: Query history isn't cleared after passing the retention period
* [MXS-4058](https://jira.mariadb.org/browse/MXS-4058) Query Editor: Connection to [::]:9999 failed. Error 2002: Can't connect to server on '::' (113)
* [MXS-4053](https://jira.mariadb.org/browse/MXS-4053) The cache does not handle multi-statements properly.
* [MXS-4045](https://jira.mariadb.org/browse/MXS-4045) Add maxctrl command for dumping the whole REST API output
* [MXS-4043](https://jira.mariadb.org/browse/MXS-4043) Creating a listener in the GUI requires defining the service twice
* [MXS-4040](https://jira.mariadb.org/browse/MXS-4040) Mariadbmon constantly logs errors if event scheduler is disabled
* [MXS-4018](https://jira.mariadb.org/browse/MXS-4018) Query Editor: Column names should be auto adjust in the Data Preview
* [MXS-3936](https://jira.mariadb.org/browse/MXS-3936) Expected status message in the context of queued command, but received a ARRAY

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
