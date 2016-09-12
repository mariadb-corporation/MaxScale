# Upgrading MariaDB MaxScale from 1.4 to 2.0

This document describes particular issues to take into account when upgrading
MariaDB MaxScale from version 1.4 to 2.0.

For more information about MariaDB MaxScale 2.0, please refer to [ChangeLog](../Changelog.md).

## Installation

Before starting the upgrade, we **strongly** recommend you back up your current
configuration file.

## MaxAdmin

The default way the communication between MaxAdmin and MariaDB MaxScale is
handled has been changed from an internet socket to a Unix domain socket.
The former alternative is still available but has been _deprecated_.

If no arguments are given to MaxAdmin, it will attempt to connect to
MariaDB MaxScale using a Unix domain socket. After the upgrade you will
need to provide at least one internet socket related flag - `-h`, `-P`,
`-u` or `-p` - to force MaxAdmin to use the internet socket approach.

E.g.

    user@host $ maxadmin -u admin

## MySQL Monitor

The MySQL Monitor now assigns the stale state to the master server by default.
In addition to this, the slave servers receive the stale slave state when they
lose the connection to the master. This should not cause changes in behavior
but the output of MaxAdmin will show new states when replication is broken.
