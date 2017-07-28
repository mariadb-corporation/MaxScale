# Common Monitor Parameters

This document lists optional parameters that all current monitors support.

## Parameters

### `user`

Username used by the monitor to connect to the backend servers. If a server defines
the `monitoruser` parameter, that value will be used instead.

### `password`

Password for the user defined with the `user` parameter. If a server defines
the `monitorpw` parameter, that value will be used instead.

### `monitor_interval`

This is the time the monitor waits between each cycle of monitoring. The default
value of 2000 milliseconds (2 seconds) should be lowered if you want a faster
response to changes in the server states. The value is defined in milliseconds
and the smallest possible value is 100 milliseconds.

The default value of _monitor_interval_ was updated from 10000 milliseconds to
2000 milliseconds in MaxScale 2.2.0.

```
monitor_interval=2500
```

### `backend_connect_timeout`

This parameter controls the timeout for connecting to a monitored server. It is in seconds and the minimum value is 1 second. The default value for this parameter is 3 seconds.

```
backend_connect_timeout=6
```

### `backend_write_timeout`

This parameter controls the timeout for writing to a monitored server. It is in seconds and the minimum value is 1 second. The default value for this parameter is 2 seconds.

```
backend_write_timeout=4
```

### `backend_read_timeout`

This parameter controls the timeout for reading from a monitored server. It is in seconds and the minimum value is 1 second. The default value for this parameter is 1 seconds.

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

For example, the previous example will be executed as:

```
/home/user/myscript.sh initiator=[192.168.0.10]:3306 event=master_down live_nodes=[192.168.0.201]:3306,[192.168.0.121]:3306
```

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
