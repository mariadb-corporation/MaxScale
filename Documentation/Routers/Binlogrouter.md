#  Binlogrouter

**NOTE:** The binlog router delivered with 2.5 is completely new and is ***not**
          100% backward compatible with the binlog router delivered with earlier
          versions of MaxScale.

The binlogrouter is a router that acts as a replication proxy for MariaDB
master-slave replication. The router connects to a master, retrieves the binary
logs and stores them locally. Slave servers can connect to MaxScale like they
would connect to a normal master server. If the master server goes down,
replication between MaxScale and the slaves can still continue up to the latest
point to which the binlogrouter replicated to. The master can be changed without
disconnecting the slaves and without them noticing that the master server has
changed. This allows for a more highly available replication setup.

In addition to the high availability benefits, the binlogrouter creates only one
connection to the master whereas with normal replication each individual slave
will create a separate connection. This reduces the amount of work the master
database has to do which can be significant if there are a large number of
replicating slaves.

## Differences Between Old and New Binlogrouter Implementations

The binlogrouter in MaxScale 2.5.0 is a new and improved version of the original
binlogrouter found in older MaxScale versions. The new implementation contains
most of the features that were in the original binlogrouter but some of them
were removed as they were either redundant or not useful.

The major differences between the new and old binlog router are:

 * The old binlog router had both `server_id` and `master_id`, the new only
   `server_id`.

 * No need to configure _heartbeat_ and _burst interval_ anymore as they are
   now automatically configured.

 * Traditional replication that uses the binary log name and file offset to
   start the replication process is not supported.

 * Semi-sync support is not implemented.

 * Binlog encryption is not implemented.

 * _Secondary masters_ are not supported, but the functionality provided by
   `select_master` is roughly equivalent.

The documentation for the binlogrouter in MaxScale 2.4 is provided for reference
[here](Binlogrouter-2.4.md).

## Supported SQL Commands

TODO: Document these

## Configuration Parameters

The binlogrouter is configured similar to how normal routers are configured in
MaxScale. It requires at least one listener where clients can connect to and one
server from which the database user information can be retrieved. An example
configuration can be found in the [example](#example) section of this document.

### `datadir`

Directory where binary log files are stored. By default the files are stored in
`/var/lib/maxscale/binlogs`.

### `server_id`

The server ID that MaxScale uses when connecting to the master and when serving
binary logs to the slaves. Default value is 1234.

### `net_timeout`

Network connection and read timeout for the connection to the master. The value
is specified as documented
[here](../Getting-Started/Configuration-Guide.md#durations). Default value is 30
seconds.

### `select_master`

Automatically select the master server to replicate from. The default value is
false.

When this feature is enabled, the master from which binlogrouter will replicate
from will be selected from the list of servers listed in `servers`. These
servers should be monitored by a monitor. Only servers with the `Master` status
are used. If multiple master servers are available, the first available master
server will be used.

The GTID the replication will start from will be based on the latest replicated
GTID. If no GTID has been replicated, the router will start replication from the
start. Manual configuration of the GTID can be done by first configuring the
replication manually with `CHANGE MASTER TO`.

## Example

The following is a minimal configuration for the binlogrouter. With it, the
service will accept connections on port 3306.

```
[master1]
type=server
address=192.168.0.1
port=3306

[MariaDB-Monitor]
type=monitor
module=mariadbmon
servers=master1
user=maxuser
password=maxpwd
monitor_interval=10s

[Replication-Proxy]
type=service
router=binlogrouter
servers=master1
user=maxuser
password=maxpwd

[Replication-Listener]
type=listener
service=Replication-Proxy
protocol=MariaDBClient
port=3306
```

## Limitations

* Whenever the binlogrouter connects to a master server, it will open a
  new binlog file where it writes new events. This can happen when
  replication is started due to a `START SLAVE` command or when the
  network connection to the master is lost and a reconnection takes place.

* The PURGE BINARY LOGS command must not be executed when there are slaves
  actively replicating from the binlogrouter. Before executing this command,
  make sure all slaves have stopped replicating from the files about to be
  purged.

* Old-style replication with binlog name and file offset is not supported
  and the replication must be started by setting up the GTID to replicate
  from.

* Only replication from MariaDB servers is supported.
