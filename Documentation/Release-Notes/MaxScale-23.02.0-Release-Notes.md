# MariaDB MaxScale 23.02 Release Notes -- 2023-02-

Release 23.02.0 is a Beta release.

This document describes the changes in release 23.02, when compared to
release 22.08.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Changed Features

### ...

## Dropped Features

### ...

## ...

## New Features

### [MXS-3708](https://jira.mariadb.org/browse/MXS-3708) Cache runtime modification

Some configuration parameters, most notable the
[rules](../Filters/Cache.md#rules),
can now be changed at runtime.

### [MXS-4106](https://jira.mariadb.org/browse/MXS-4106) Redis authentication

Authentication can be enabled when Redis is used as the cache storage. See
[here](../Filters/Cache.md#storage_redis) for more information.

### [MXS-4107](https://jira.mariadb.org/browse/MXS-4107) TLS encrypted Redis connections

SSL/TLS can now be used in the communication between MaxScale and
the Redis server when the latter is used as the storage for the
cache. See
[here](../Filters/Cache.md#storage_redis) for more information.

### [MXS-4270](https://jira.mariadb.org/browse/MXS-4270) ed25519 authentication support

MariaDB Server ed25519 authentication plugin support added. See
[here](../Authenticators/Ed25519-Authenticator.md) for more information.

### [MXS-4320](https://jira.mariadb.org/browse/MXS-4320) Let maxctrl show datetime values using local client timezone

The maxctrl `list` and `show` commands now display timestamps using the
locale and timezone of the client computer.

### MaxGUI

Numerous additions have been added and improvements made to MaxGUI.
The most notable ones are listed here:

* ...

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
