# MariaDB MaxScale 2.3.6 Release Notes

Release 2.3.6 is a GA release.

This document describes the changes in release 2.3.6, when compared to the
previous release in the same series.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-2417](https://jira.mariadb.org/browse/MXS-2417) MaxScale main config should take precedence over runtime config on restart

### REST API & MaxCtrl: Hard maintenance mode

The new `--force` option for the `set server` command in MaxCtrl allows all
connections to the server in question to be closed when it is set into
maintenance mode. This causes idle connections to be closed immediately.

For more information, read the
[REST-API](../REST-API/Resources-Server.md#set-server-state) documentation for
the `set` endpoint.

## Bug fixes

* [MXS-2419](https://jira.mariadb.org/browse/MXS-2419) Hangs on query during multiple transaction replays
* [MXS-2418](https://jira.mariadb.org/browse/MXS-2418) Crash on transaction replay if log_info is on and session starts with no master

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for supported the Linux distributions.

Packages can be downloaded [here](https://mariadb.com/downloads/mariadb-tx/maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is `maxscale-X.Y.Z`. Further, the default branch is always the latest GA version
of MaxScale.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
