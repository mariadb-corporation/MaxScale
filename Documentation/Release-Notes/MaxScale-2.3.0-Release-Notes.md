# MariaDB MaxScale 2.3.0 Release Notes

Release 2.2.0 is a Beta release.

This document describes the changes in release 2.3.0, when compared to
release 2.2.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

### Session Command History

The _readwritesplit_ session command history is now enabled mby default but it
is limited to a total of 50 distict session commands. This default allows most
sessions to leverage the newly improved reconnection mechanism without having to
explicitly enable the history. When the limit of 50 commands is exceeded, the
session command history is disabled. This makes it possible to use pooled
connections by default without having to explicitly disable the history (this
was the case with pre-2.1 versions of MaxScale).

The way that the history is stored has also changed. Instead of storing all
session commands, each session only stores the first and last execution of each
command. This way the history is compressed into a compact representation while
still retaining the relative order of each command.

To keep the old functionality, add `disable_sescmd_history=true` to the service
definition.

###  Switchover new master autoselection

The switchover command in *mariadbmon* can now be called with just the monitor
name as parameter. In this case the monitor will automatically select a server
for promotion.

## Dropped Features

### `router_options` in Avrorouter

The use of `router_options` with avrorouter was deprecated in MaxScale 2.1. In
MaxScale 2.3, the use of `router_options` is no longer supported and the options
should be given as parameters instead.

## New Features

### Runtime Configuration of the Cache
With the variable `@maxscale.cache.enabled` it is now possible for a
client to specify whether the cache should be used. Please see the
[Cache](../Filters/Cache.md) documentation for details.

## Bug fixes

[Here is a list of bugs fixed in MaxScale 2.3.0.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.3.0)

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
