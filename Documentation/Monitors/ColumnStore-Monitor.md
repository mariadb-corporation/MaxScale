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

The automatic master detection only works with ColumnStore 1.2. Older versions
of ColumnStore do not implement the required functionality to automatically
detect which of the servers is the primary UM.

With older versions the `primary` parameter must be defined to tell the monitor
which of the servers is the primary UM node. This guarantees that DDL statements
are only executed on the primary UM.

## Configuration

Read the [Monitor Common](Monitor-Common.md) document for a list of supported
common monitor parameters.

### `primary`

The `primary` parameter controls which server is chosen as the master
server. This is an optional parameter.

If the server pointed to by this parameter is available and is ready to process
queries, it receives the _Master_ status. If the parameter is not defined and
the ColumnStore server does not support the `mcsSystemPrimary` function, no
master server is chosen.

Note that this parameter is only used when the server does not implement the
required functionality. Otherwise the parameter is ignored as the information
from ColumnStore itself is more reliable.

## Commands

The Columnstore monitor provides module commands using which the Columnstore
cluster can be managed. The commands can be invoked using the REST-API with
a client such as curl or using maxctrl.

All commands require the monitor instance name as the first parameters.
Additional parameters must be provided depending on the command.

### _Cluster Start_
Starts the Columnstore cluster.
```
call command csmon cluster-start MyMonitor
```

### _Cluster Stop_
Stops the Columnstore cluster.
```
call command csmon cluster-stop MyMonitor
```

### _Cluster Shutdown_
Shuts down the Columnstore cluster.
```
call command csmon cluster-shutdown MyMonitor
```

### _Cluster Add Node_
Adds a new node to the Columnstore cluster.
```
call command csmon cluster-add-node MyMonitor
```

### _Cluster Remove Node_
Removes a node from the Columnstore cluster.
```
call command csmon cluster-remove-node MyMonitor
```

## Example

The following is an example of a `csmon` configuration.

```
[CS-Monitor]
type=monitor
module=csmon
servers=um1,um2,um3
user=myuser
passwd=mypwd
monitor_interval=5000
primary=um1
```

It defines a set of three UMs and defines the UM `um1` as the primary UM.
