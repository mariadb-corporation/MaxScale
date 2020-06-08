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
CREATE USER 'maxuser'@'%' IDENTIFIED BY 'maxpwd';
GRANT ALL ON infinidb_vtable.* TO 'maxuser'@'%';
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

Required by and only allowed when the value of `version` is `1.0`.

The `primary` parameter controls which server is chosen as the master
server. This is an optional parameter.

If the server pointed to by this parameter is available and is ready to process
queries, it receives the _Master_ status.

### `admin_port`

Allowed only when the value of version is `1.5`.

This optional parameter specifies the port of the Columnstore administrative
daemon. The default value is `8630`. Note that the daemons of all nodes must
be listening on the same port.

### `admin_base_path`

Allowed only when the value of version is `1.5`.

This optional parameter specifies the base path of the Columnstore
administrative daemon. The default value is `/cmapi/0.3.0`.

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

### `timeout`

Allowed only when the value of version is `1.5`.

This optional parameter specifies the timeout to used if one
is not explicitly provided to a module command. The timeout
can be specified as explained
[here](../Getting-Started/Configuration-Guide.md#durations).

## Commands

The Columnstore monitor provides module commands using which the Columnstore
cluster can be managed. The commands can be invoked using the REST-API with
a client such as curl or using maxctrl.

All commands require the monitor instance name as the first parameters.
Additional parameters must be provided depending on the command.

Note that as maxctrl itself has a timeout of 10000 milliseconds, if a
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

[CsNode3]
type=server
...

[CsMonitor]
type=monitor
module=csmon
servers=CsNode1,CsNode2,CsNode3
...

```

### _Start_
```
call command csmon start <monitor-name> [<timeout>]
```
Starts the Columnstore cluster.

Example
```
call command csmon start CsMonitor 20s
```

### _Shutdown_
```
call command csmon shutdown <monitor-name> [<timeout>]
```
Shuts down the Columnstore cluster.

Example
```
call command csmon shutdown CsMonitor 20s
```

### _Status_
```
call command csmon status <monitor-name> [<server>]
```
Returns the status of all servers in the cluster or of
a specific one.

Example
```
call command csmon status CsMonitor CsNode1
```

### _Set Mode_
```
call command csmon mode-set <monitor-name> (readonly|readwrite) [<timeout>]
```
Sets the mode of the cluster.

Example
```
call command csmon mode-set CsMonitor readonly 20s
```

### _Get Config_
```
call command csmon config-get <monitor-name> [<server-name>]
```
Returns the configs of all servers in the cluster or of a specific one.

Example
```
call command csmon config-get CsMonitor CsNode2
```

### _Set Config_
```
call command csmon config-set <monitor-name> <config> [<server-name>]
```
Sets the config on all servers in the cluster or on a specific one.
The config should be a JSON object enclosed in quotes.

*NOTE* MaxScale does not verify the provided configuration object in
any way, other than ensuring that is really is a JSON object, but
simply sends it forward to the servers in question.

```
call command csmon config-set CsMonitor "{ ... }"
```

## Example

The following is an example of a `csmon` configuration.

```
[CSMonitor]
type=monitor
module=csmon
version=1.5
servers=CsNode1,CsNode2,CsNode3
user=myuser
passwd=mypwd
monitor_interval=5000
api_key=somekey1234
```
