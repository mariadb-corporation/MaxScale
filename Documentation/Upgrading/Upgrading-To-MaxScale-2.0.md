# Upgrading MariaDB MaxScale from 1.4 to 2.0

This document describes particular issues to take into account when upgrading
MariaDB MaxScale from version 1.4 to 2.0.

For more information about MariaDB MaxScale 2.0, please refer to [ChangeLog](../Changelog.md).

## Installation

Before starting the upgrade, we **strongly** recommend you back up your current
configuration file.

## MaxAdmin

The way a user of MaxAdmin is authenticated has been completely changed.
In 2.0, MaxAdmin can only connect to MariaDB MaxScale using a domain socket, thus
_only when run on the same host_, and authorization is based upon the UNIX
identity. Remote access is no longer supported.

When 2.0 has been installed, MaxAdmin can only be used by `root` and
other users must be added anew. Please consult
[MaxAdmin documentation](../Reference/MaxAdmin.md) for more details.

This change requires the _maxscaled_ protocol listener entry in the
MaxScale configuration file to be updated; address and port information
must be replaced with socket information. For instance, an entry like
```
[MaxAdmin Listener]
type=listener
protocol=maxscaled
address=localhost
port=6603
```
should be updated to
```
[MaxAdmin Listener]
type=listener
protocol=maxscaled
socket=default
```
where `default` corresponds to `/tmp/maxadmin.sock`.

Note that if this update is *not* made, maxscaled will log a warning
and use the default socket path. This behaviour may change in later
releases of MaxScale.

## MySQL Monitor

The MySQL Monitor now assigns the stale state to the master server by default.
In addition to this, the slave servers receive the stale slave state when they
lose the connection to the master. This should not cause changes in behavior
but the output of MaxAdmin will show new states when replication is broken.
