# XRouter

[TOC]

## Overview

The XRouter is used to distribute DDL commands to multiple nodes while load
balancing client sessions. The router supports both MariaDB and PostgreSQL
protocols.

## Routing Logic

The XRouter classifies the nodes into three different categories:

- Main node

  - Always the first node in the `servers` list. It is used as the global
  synchronization point of the cluster.

- Secondary nodes

  - All nodes that are not the main node (i.e. all values after the first one in
  the `servers` list).

- Solo node

  - A node that's randomly chosen from all available nodes on session
    startup. This can be either the main node or a secondary node.

There are two different modes of routing: single-node routing and multi-node
routing. The single-node routing is a simple mode where the requests are routed
to the solo node and the results are returned from it immediately.

Multi-node routing is more complex and involves first locking the main node
after which the SQL command is executed on the main node. If the command is
successful, the command is also executed on the secondary nodes while the lock
is held on the main node. Once the secondary nodes have completed the command,
the main node is unlocked and the result is returned to the client.

If the command generates an error on the main node (e.g. a syntax error for a
`CREATE TABLE` statement), the command is not propagated to the secondary
nodes. If the command fails on a secondary node and it cannot be successfully
retried (e.g. the disk is full), the secondary node is fenced out of the cluster
by placing it into maintenance mode. If the secondary node error could be
retried but it doesn't succeed before the configured timeout (`retry_timeout`)
is exceeded, the node is also fenced out.

## MariaDB Specific Behavior

- The locking is implemented using `GET_LOCK()`. The value of
  [`lock_id`](#lock_id) can be any arbitrary value that can be converted into a
  SQL string constant.

## PostgreSQL Specific Behavior

- Any `CREATE USER` or `CREATE ROLE` statements that set a password are
  pre-salted by the XRouter. This is done to guarantee that they are created
  with the same salted password and that the login information extracted by
  MaxScale can be used to connect to all of the nodes.

- The locking is implemented using `pg_advisory_lock()`. This means that the
  value of [`lock_id`](#lock_id) must be a number.

## Configuration Parameters

### `lock_id`

- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `1679475768`

The lock ID used by the XRouter to lock the main node. If the lock ID conflicts
with an ID used by a client application, the value can be changed by specifying
this parameter. Given that the value is an arbitrary UNIX timestamp chosen at
the time of development, the likelihood of this should be very low.

All XRouter instances that use the same database cluster must use the same
`lock_id` value.

### `retry_timeout`

- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `60s`

The timeout for retry operations done by the XRouter. Retries are done only
during multi-node routing and only on secondary nodes. If the timeout is
exceeded, the secondary node is fenced out of the cluster by putting it into
maintenance mode.

### `retry_sqlstates`

- **Type**: string list
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `HV, HW`

The set of SQLSTATE values and/or prefixes that trigger a retry on a secondary
node. If a full five character SQLSTATE value is given (e.g. `HV001`) only that
specific value will trigger a retry. If a partial SQLSTATE prefix (e.g. `HV`)
is given, any SQLSTATE that shares the same prefix will trigger a retry.

## Limitations

- Pipelined execution of commands (sending multiple commands before reading
  their results) is not currently supported. If done with the XRouter, the
  commands are executed one by one.

- DDL statements that are done using the prepared statement protocol
  (both MariaDB and PostgreSQL) are not propagated to all nodes. To
  correctly propagate DDLs to all nodes, the basic text protocol must be
  used.
