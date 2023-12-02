# MariaDB MaxScale 23.02.7 Release Notes

Release 23.02.7 is a GA release.

This document describes the changes in release 23.02.7, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-23.02.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-4881](https://jira.mariadb.org/browse/MXS-4881) Startup option --basedir mishandled
* [MXS-4869](https://jira.mariadb.org/browse/MXS-4869) Parameter table isn't refreshed after update in the GUI
* [MXS-4856](https://jira.mariadb.org/browse/MXS-4856) GUI failed to create a monitor in a monitored server detail page
* [MXS-4851](https://jira.mariadb.org/browse/MXS-4851) No space or separator between 2 routing targets in the services dashboard
* [MXS-4798](https://jira.mariadb.org/browse/MXS-4798) Broken slave promoted to master when no other servers are available

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
