# Upgrading MariaDB MaxScale from 2.0 to 2.1

This document describes particular issues to take into account when upgrading
MariaDB MaxScale from version 2.0 to 2.1.

For more information about MariaDB MaxScale 2.1, please refer to the
[ChangeLog](../Changelog.md).

For a complete list of changes in MaxScale 2.1.0, refer to the
[MaxScale 2.1.0 Release Notes](../Release-Notes/MaxScale-2.1.0-Release-Notes.md).

## Installation

Before starting the upgrade, we **strongly** recommend you back up your current
configuration file.

## MaxScale Log Files

The name of the log file was changed from _maxscaleN.log_ to _maxscale.log_. The
default location for the log file is _/var/log/maxscale/maxscale.log_.

Rotating the log files will cause MaxScale to reopen the file instead of
renaming them. This makes the MaxScale logging facility _logrotate_ compatible.

## ReadWriteSplit

The `disable_sescmd_history` option is now enabled by default. This means that
slaves will not be recovered mid-session even if a replacement slave is
available. To enable the legacy behavior, add the `disable_sescmd_history=true`
parameter to the service definition.

## Persistent Connections

The MySQL session state is reset in MaxScale 2.1 for persistent
connections. This means that any modifications to the session state (default
database, user variable etc.) will not survive if the connection is put into the
connection pool. For most users, this is the expected behavior.

## User Data Cache

The location of the MySQL user data cache was moved from
`/var/cache/maxscale/<Service>` to `/var/cache/maxscale/<Service>/<Listener>`.

## Galeramon Monitoring Algorithm

Galeramon will assign the master status *only* to the node which has a
_wsrep_local_index_ value of 0. This will guarantee consistent writes with
multiple MaxScales but it also causes slower changes of the master node.

To enable the legacy behavior, add `root_node_as_master=false` to the Galera
monitor configuration.

## MaxAdmin Editing Mode

The default editing mode was changed from _vim_ to _emacs_ mode. To start
maxadmin in the legacy mode, use the `-i` option.
