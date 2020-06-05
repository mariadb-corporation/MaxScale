# Pinloki

## Configuration Parameters

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

When this feature is enabled, the master from which pinloki will replicate from
will be selected from the list of servers listed in `servers`. These servers
should be monitored by a monitor. Only servers with the `Master` status are
used. If multiple master servers are available, the first available master
server will be used.

The GTID the replication will start from will be based on the latest replicated
GTID. If no GTID has been replicated, the router will start replication from the
start. Manual configuration of the GTID can be done by first configuring the
replication manually with `CHANGE MASTER TO`.
