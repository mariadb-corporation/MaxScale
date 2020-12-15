# ColumnStore Monitor

The ColumnStore monitor, `csmon`, is a monitor module for MariaDB ColumnStore
servers. It supports multiple UM nodes and can detect the correct server for
DML/DDL statements which will be labeled as the master. Other UM nodes will be
used for reads.

## Required Grants

The credentials defined with the `user` and `password` parameters must have all
grants on the `infinidb_vtable` database.

For example, to create a user for this monitor with the required grants execute
the following SQL.

```
CREATE USER 'maxscale'@'maxscalehost' IDENTIFIED BY 'maxscale-password';
GRANT ALL ON infinidb_vtable.* TO 'maxscale'@'maxscalehost';
```

## Master Selection

The Columnstore Monitor in MaxScale 2.5 supports Columnstore 1.0, 1.2 and 1.5,
and the master selection is done differently for each version.

* If the version is 1.0, the master server must be specified using the `primary`
parameter.
* If the version is 1.2, the master server is selected automatically using
the Columnstore function `mcsSystemPrimary()`.
* If the version is 1.5, the master server is selected automatically by
querying the Columnstore daemon running on each node.

## Configuration

Read the [Monitor Common](Monitor-Common.md) document for a list of supported
common monitor parameters.

### `version`

With this mandatory parameter the used Columnstore version is specified.
The allowed values are `1.0`, `1.2` and `1.5`.

### `primary`

Required and only allowed when the value of `version` is `1.0`.

The `primary` parameter controls which server is chosen as the master
server.

If the server pointed to by this parameter is available and is ready to process
queries, it receives the _Master_ status.

### `admin_port`

Allowed only when the value of version is `1.5`.

This optional parameter specifies the port of the Columnstore administrative
daemon. The default value is `8640`. Note that the daemons of all nodes must
be listening on the same port.

### `admin_base_path`

Allowed only when the value of version is `1.5`.

This optional parameter specifies the base path of the Columnstore
administrative daemon. The default value is `/cmapi/0.4.0`.

### `api_key`

Allowed only when the value of version is `1.5`.

This optional parameter specifies the API key to be used in the
communication with the Columnstore administrative daemon. If no
key is specified, then a key will be generated and stored to the
file `api_key.txt` in the directory with the same name as the
monitor in data directory of MaxScale. Typically that will
be `/var/lib/maxscale/<monitor-section>/api_key.txt`.

Note that Columnstore will store the first key provided and
thereafter require it, so changing the key requires the
resetting of the key on the Columnstore nodes as well.

### `local_address`

Allowed only when the value of version is `1.5`.

With this parameter it is specified what IP MaxScale should
tell the Columnstore nodes it resides at. Either it or
`local_address` at the global level in the MaxScale
configuration file must be specified. If both have been
specified, then the one specified for the monitor overrides.

## Commands

The Columnstore monitor provides module commands using which the Columnstore
cluster can be managed. The commands can be invoked using the REST-API with
a client such as curl or using maxctrl.

All commands require the monitor instance name as the first parameters.
Additional parameters must be provided depending on the command.

Note that as maxctrl itself has a timeout of 10 seconds, if a
timeout larger than that is provided to any command, the timeout of
maxctrl must also be increased. For instance:
```
maxctrl --timeout 30s call command csmon shutdown CsMonitor 20s
```
Here a 30 second timeout is specified for maxctrl to ensure
that it does not expire before the timeout of 20s provided for
the shutdown command possibly does.

The output is always a JSON object.

In the following, assume a configuration like this:
```
[CsNode1]
type=server
...

[CsNode2]
type=server
...

[CsMonitor]
type=monitor
module=csmon
servers=CsNode1,CsNode2
...

```

### `start`
Starts the Columnstore cluster.
```
call command csmon start <monitor-name> <timeout>
```

Example
```
call command csmon start CsMonitor 20s
```

### `shutdown`
Shuts down the Columnstore cluster.
```
call command csmon shutdown <monitor-name> <timeout>
```

Example
```
call command csmon shutdown CsMonitor 20s
```

### `status`
Get the status of the Columnstore cluster.
```
call command csmon status <monitor-name> [<server>]
```
Returns the status of the cluster or the status of a specific server.

Example
```
call command csmon status CsMonitor
call command csmon status CsMonitor CsNode1
```

### `mode-set`
Sets the mode of the cluster.
```
call command csmon mode-set <monitor-name> (readonly|readwrite) <timeout>
```

Example
```
call command csmon mode-set CsMonitor readonly 20s
```

### `config-get`
Returns the cluster configuration.
```
call command csmon config-get <monitor-name> [<server-name>]
```
If no server is specified, the configuration is fetched from
the first server in the monitor configuration, otherwise from
the specified server.

Note that if everything is in order, the returned configuration
should be identical regardless of the server it is fetched from.

Example
```
call command csmon config-get CsMonitor CsNode2
```

### `add-node`
Adds a new node located on the server at the hostname or IP _host_
to the Columnstore cluster.
```
call command csmon add-node <monitor-name> <host> <timeout>
```

Example
```
call command csmon add-node CsMonitor mcs2 20s
```
For a more complete example, please refer to [adding a node](#adding-a-node).

### `remove-node`
Remove the node located on the server at the hostname or IP _host_
from the Columnstore cluster.
```
call command csmon remove-node <monitor-name> <host> <timeout>
```

Example
```
call command csmon remove-node CsMonitor mcs2 20s
```
For a more complete example, please refer to [removing a node](#removing-a-node).

## Example

The following is an example of a `csmon` configuration.

```
[CSMonitor]
type=monitor
module=csmon
version=1.5
servers=CsNode1,CsNode2
user=myuser
password=mypwd
monitor_interval=5000
api_key=somekey1234
```

## Adding a Node

Adding a new node to a Columnstore cluster can be performed dynamically
at runtime, but it must be done in two steps. First, the node is added
to Columnstore and then, the corresponding server object (that possibly
has to be created) in the MaxScale configuration is added to the
Columnstore monitor.

In the following, assume a two node Columnstore cluster and an initial
MaxScale configuration like.
```
[CsNode1]
type=server
...

[CsNode2]
type=server
...

[CsMonitor]
type=monitor
module=csmon
servers=CsNode1,CsNode2
...
```
Invoking `maxctrl list servers` will now show:
```
┌─────────┬─────────────┬──────┬─────────────┬─────────────────┬──────┐
│ Server  │ Address     │ Port │ Connections │ State           │ GTID │
├─────────┼─────────────┼──────┼─────────────┼─────────────────┼──────┤
│ CsNode1 │ 10.10.10.10 │ 3306 │ 0           │ Master, Running │      │
├─────────┼─────────────┼──────┼─────────────┼─────────────────┼──────┤
│ CsNode2 │ 10.10.10.11 │ 3306 │ 0           │ Slave, Running  │      │
└─────────┴─────────────┴──────┴─────────────┴─────────────────┴──────┘
```
If we now want to add a new Columnstore node, located at `mcs3/10.10.10.12`
to the cluster, the steps are as follows.

First the node is added
```
maxctrl --timeout 30s call command csmon add-node CsMonitor mcs3 20s
```
After a while the following is output:
```
{
    "links": {
        "self": "http://localhost:8989/v1/maxscale/modules/csmon/add-node"
    },
    "meta": {
        "message": "Node mcs3 successfully added to cluster.",
        "result": {
            "node_id": "mcs3",
            "timestamp": "2020-08-07 10:03:49.474539"
        },
        "success": true
    }
}
```
At this point, the Columnstore cluster consists of three nodes. However,
the Columnstore monitor is not yet aware of the new node.

First we need to create the corresponding server object.
```
maxctrl create server CsNode3 10.10.10.12
```
Invoking `maxctrl list servers` will now show:
```
┌─────────┬─────────────┬──────┬─────────────┬─────────────────┬──────┐
│ Server  │ Address     │ Port │ Connections │ State           │ GTID │
├─────────┼─────────────┼──────┼─────────────┼─────────────────┼──────┤
│ CsNode3 │ 10.10.10.12 │ 3306 │ 0           │ Down            │      │
├─────────┼─────────────┼──────┼─────────────┼─────────────────┼──────┤
│ CsNode1 │ 10.10.10.10 │ 3306 │ 0           │ Master, Running │      │
├─────────┼─────────────┼──────┼─────────────┼─────────────────┼──────┤
│ CsNode2 │ 10.10.10.11 │ 3306 │ 0           │ Slave, Running  │      │
└─────────┴─────────────┴──────┴─────────────┴─────────────────┴──────┘
```
The server `CsNode3` has been created, but its state is `Down` since
it is not yet being monitored.
```
┌───────────┬─────────┬──────────────────┐
│ Monitor   │ State   │ Servers          │
├───────────┼─────────┼──────────────────┤
│ CsMonitor │ Running │ CsNode1, CsNode2 │
└───────────┴─────────┴──────────────────┘
```
It must now be added to the monitor.
```
maxctrl link monitor CsMonitor CsNode3
```
Now the server is monitored and `maxctrl list monitors` shows:
```
┌───────────┬─────────┬───────────────────────────┐
│ Monitor   │ State   │ Servers                   │
├───────────┼─────────┼───────────────────────────┤
│ CsMonitor │ Running │ CsNode1, CsNode2, CsNode3 │
└───────────┴─────────┴───────────────────────────┘
```
The state of the new node is now also set correctly, as shown by
`maxctrl list servers`.
```
┌─────────┬─────────────┬──────┬─────────────┬─────────────────┬──────┐
│ Server  │ Address     │ Port │ Connections │ State           │ GTID │
├─────────┼─────────────┼──────┼─────────────┼─────────────────┼──────┤
│ CsNode3 │ 10.10.10.12 │ 3306 │ 0           │ Slave, Running  │      │
├─────────┼─────────────┼──────┼─────────────┼─────────────────┼──────┤
│ CsNode1 │ 10.10.10.10 │ 3306 │ 0           │ Master, Running │      │
├─────────┼─────────────┼──────┼─────────────┼─────────────────┼──────┤
│ CsNode2 │ 10.10.10.11 │ 3306 │ 0           │ Slave, Running  │      │
└─────────┴─────────────┴──────┴─────────────┴─────────────────┴──────┘
```
Note that the MaxScale server object can be created at any point, but
it must not be added to the monitor before the node has been added to
the Columnstore cluster using `call command csmon add-node`.

## Removing a Node

Removing a node should be performed in the reverse order of how a
node was added. First, the MaxScale server should be removed from the
monitor. Then, the node should be removed from the Columnstore cluster.

Suppose we want to remove the Columnstore node at `mcs2/10.10.10.12`
and the current situation is as:
```
┌───────────┬─────────┬───────────────────────────┐
│ Monitor   │ State   │ Servers                   │
├───────────┼─────────┼───────────────────────────┤
│ CsMonitor │ Running │ CsNode1, CsNode2, CsNode3 │
└───────────┴─────────┴───────────────────────────┘
```
First, the server is removed from the monitor.
```
maxctrl unlink monitor CsMonitor CsNode3
```
Checking with `maxctrl list monitors` we see that the server has
indeed been removed.
```
┌───────────┬─────────┬──────────────────┐
│ Monitor   │ State   │ Servers          │
├───────────┼─────────┼──────────────────┤
│ CsMonitor │ Running │ CsNode1, CsNode2 │
└───────────┴─────────┴──────────────────┘
```
Now the node can be removed from the cluster itself.
```
maxctrl --timeout 30s call command csmon remove-node CsMonitor mcs3 20s
{
    "links": {
        "self": "http://localhost:8989/v1/maxscale/modules/csmon/remove-node"
    },
    "meta": {
        "message": "Node mcs3 removed from the cluster.",
        "result": {
            "node_id": "mcs3",
            "timestamp": "2020-08-07 11:41:36.573425"
        },
        "success": true
    }
}
```
