# Common Monitor Parameters

This document lists optional parameters that all current monitors support.

## Parameters

### `user`

Username used by the monitor to connect to the backend servers. If a server defines
the `monitoruser` parameter, that value will be used instead.

### `password`

Password for the user defined with the `user` parameter. If a server defines
the `monitorpw` parameter, that value will be used instead.

**Note:** In older versions of MaxScale this parameter was called `passwd`. The
  use of `passwd` was deprecated in MaxScale 2.3.0.

### `monitor_interval`

Defines, in milliseconds, how often the monitor updates the status of the
servers. The default value is 2000 (2 seconds). Choose a lower value if servers
should be queried more often. The smallest possible value is 100. If querying
the servers takes longer than `monitor_interval`, the effective update rate is
reduced.

The default value of `monitor_interval` was updated from 10000 milliseconds to
2000 milliseconds in MaxScale 2.2.0.

```
monitor_interval=2500
```

### `backend_connect_timeout`

This parameter controls the timeout for connecting to a monitored server. It is
in seconds and the minimum value is 1 second. The default value for this
parameter is 3 seconds.

```
backend_connect_timeout=6
```

### `backend_write_timeout`

This parameter controls the timeout for writing to a monitored server. It is in
seconds and the minimum value is 1 second. The default value for this parameter
is 3 seconds.

```
backend_write_timeout=4
```

### `backend_read_timeout`

This parameter controls the timeout for reading from a monitored server. It is
in seconds and the minimum value is 1 second. The default value for this
parameter is 3 seconds.

```
backend_read_timeout=2
```

### `backend_connect_attempts`

This parameter defines the maximum times a backend connection is attempted every
monitoring loop. The default is 1. Every attempt may take up to
`backend_connect_timeout` seconds to perform. If none of the attempts are
successful, the backend is considered to be unreachable and down.

```
backend_connect_attempts=3
```

### `disk_space_threshold`

This parameter duplicates the `disk_space_threshold`
[server parameter](../Getting-Started/Configuration-Guide.md#disk_space_threshold).
If the parameter has *not* been specified for a server, then the one specified
for the monitor is applied.

That is, if the disk configuration is the same on all servers monitored by
the monitor, it is sufficient (and more convenient) to specify the disk
space threshold in the monitor section, but if the disk configuration is
different on all or some servers, then the disk space threshold can be
specified individually for each server.

For example, suppose `server1`, `server2` and `server3` are identical
in all respects. In that case we can specify `disk_space_threshold`
in the monitor.
```
[server1]
type=server
...

[server2]
type=server
...

[server3]
type=server
...

[monitor]
type=monitor
servers=server1,server2,server3
disk_space_threshold=/data:80
...
```
However, if the servers are heterogenious with the disk used for the
data directory mounted on different paths, then the disk space threshold
must be specified separately for each server.
```
[server1]
type=server
disk_space_threshold=/data:80
...

[server2]
type=server
disk_space_threshold=/Data:80
...

[server3]
type=server
disk_space_threshold=/DBData:80
...

[monitor]
type=monitor
servers=server1,server2,server3
...
```
If _most_ of the servers have the data directory disk mounted on
the same path, then the disk space threshold can be specified on
the monitor and separately on the server with a different setup.
```
[server1]
type=server
disk_space_threshold=/DbData:80
...

[server2]
type=server
...

[server3]
type=server
...

[monitor]
type=monitor
servers=server1,server2,server3
disk_space_threshold=/data:80
...
```
Above, `server1` has the disk used for the data directory mounted
at `/DbData` while both `server2` and `server3` have it mounted on
`/data` and thus the setting in the monitor covers them both.

### `disk_space_check_interval`

With this positive integer parameter it can be specified in milliseconds
the minimum amount of time between disk space checks. The default value
is `20000`, which means that the disk space situation will be checked
once every 20 seconds.

Note that as the checking is made as part of the regular monitor interval
cycle, the disk space check interval is affected by the value of
`monitor_interval`. In particular, even if the value of
`disk_space_check_interval` is smaller than that of `monitor_interval`,
the checking will still take place at `monitor_interval` intervals.
```
disk_space_check_interval=10000
```

### `script`

This command will be executed when a server changes its state. The parameter should be an absolute path to a command or the command should be in the executable path. The user which is used to run MaxScale should have execution rights to the file itself and the directory it resides in.

```
script=/home/user/myscript.sh initiator=$INITIATOR event=$EVENT live_nodes=$NODELIST
```

The following substitutions will be made to the parameter value:

* `$INITIATOR` will be replaced with the IP and port of the server who initiated the event
* `$EVENT` will be replaced with the name of the event
* `$LIST` will be replaced with a list of server IPs and ports
* `$NODELIST` will be replaced with a list of server IPs and ports that are running
* `$SLAVELIST` will be replaced with a list of server IPs and ports that are slaves
* `$MASTERLIST` will be replaced with a list of server IPs and ports that are masters
* `$SYNCEDLIST` will be replaced with a list of server IPs and ports that are synced Galera nodes
* `$PARENT` will be replaced with the IP and port of the parent node of the server who initiated
   the event. For master-slave setups, this will be the master if the initiating server is a slave.
* `$CHILDREN` will be replaced with the IPs and ports of the child nodes of the server who initiated
   the event. For master-slave setups, this will be a list of slave servers if the initiating server is a master.

The expanded variable value can be an empty string if no servers match the
variable's requirements. For example, if no masters are available `$MASTERLIST`
will expand into an empty string.

For example, the previous example will be executed as:

```
/home/user/myscript.sh initiator=[192.168.0.10]:3306 event=master_down live_nodes=[192.168.0.201]:3306,[192.168.0.121]:3306
```

Any output by the executed script will be logged into the MaxScale log. Each
outputted line will be logged as a separate log message.

The log level on which the messages are logged depends on the format of the
messages. If the first word in the output line is one of `alert:`, `error:`,
`warning:`, `notice:`, `info:` or `debug:`, the message will be logged on the
corresponding level. If the message is not prefixed with one of the keywords,
the message will be logged on the notice level. Whitespace before, after or
inbetween the keyword and the colon is ignored and the matching is
case-insensitive.

### `script_timeout`

The timeout for the executed script in seconds. The default value is 90
seconds.

If the script execution exceeds the configured timeout, it is stopped by sending
a SIGTERM signal to it. If the process does not stop, a SIGKILL signal will be
sent to it once the execution time is greater than twice the configured timeout.

### `events`

A list of event names which cause the script to be executed. If this option is not defined, all events cause the script to be executed. The list must contain a comma separated list of event names.

```
events=master_down,slave_down
```

## Script events

Here is a table of all possible event types and their descriptions that the monitors can be called with.

Event Name  |Description
------------|----------
master_down |A Master server has gone down
master_up   |A Master server has come up
slave_down  |A Slave server has gone down
slave_up    |A Slave server has come up
server_down |A server with no assigned role has gone down
server_up   |A server with no assigned role has come up
ndb_down    |A MySQL Cluster node has gone down
ndb_up      |A MySQL Cluster node has come up
lost_master |A server lost Master status
lost_slave  |A server lost Slave status
lost_ndb    |A MySQL Cluster node lost node membership
new_master  |A new Master was detected
new_slave   |A new Slave was detected
new_ndb     |A new MySQL Cluster node was found

### `journal_max_age`

The maximum journal file age in seconds. The default value is 28800 seconds.

When the monitor starts, it reads any stored journal files. If the journal file
is older than the value of _journal_max_age_, it will be removed and the monitor
starts with no prior knowledge of the servers.

## Monitor Crash Safety

Starting with MaxScale 2.2.0, the monitor modules keep an on-disk journal of the
latest server states. This change makes the monitors crash-safe when options
that introduce states are used. It also allows the monitors to retain stateful
information when MaxScale is restarted.

For MySQL monitor, options that introduce states into the monitoring process are
the `detect_stale_master` and `detect_stale_slave` options, both of which are
enabled by default. Galeramon has the `disable_master_failback` parameter which
introduces a state.

The default location for the server state journal is in
`/var/lib/maxscale/<monitor name>/monitor.dat` where `<monitor name>` is the
name of the monitor section in the configuration file. If MaxScale crashes or is
shut down in an uncontrolled fashion, the journal will be read when MaxScale is
started. To skip the recovery process, manually delete the journal file before
starting MaxScale.
