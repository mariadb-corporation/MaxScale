#  Binlogrouter

**NOTE:** The binlog router delivered with 2.5 is completely new and is **not**
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

 * The list of servers where the database users for authentication are loaded
   must be explicitly configured with the `cluster`, `servers` or
   `targets` parameter.

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

 * The new binlogrouter will write its own binlog files to prevent problems that
   could happen when the master changes. This causes the binlog names to be
   different in the binlogrouter when compared to the ones on the master.

The documentation for the binlogrouter in MaxScale 2.4 is provided for reference
[here](Binlogrouter-2.4.md).

## Supported SQL Commands

The binlogrouter supports a subset of the SQL constructs that the MariaDB server
supports. The following commands are supported:

 * `CHANGE MASTER TO`

   * The binlogrouter supports the same syntax as the MariaDB server but only the
     following values are allowed:

     * `MASTER_HOST`
     * `MASTER_PORT`
     * `MASTER_USER`
     * `MASTER_PASSWORD`
     * `MASTER_USE_GTID`
     * `MASTER_SSL`
     * `MASTER_SSL_CA`
     * `MASTER_SSL_CAPATH`
     * `MASTER_SSL_CERT`
     * `MASTER_SSL_CRL`
     * `MASTER_SSL_CRLPATH`
     * `MASTER_SSL_KEY`
     * `MASTER_SSL_CIPHER`
     * `MASTER_SSL_VERIFY_SERVER_CERT`

     **NOTE:** `MASTER_LOG_FILE` and `MASTER_LOG_POS` are not supported
     as binlogrouter only supports GTID based replication.

 * `STOP SLAVE`

   * Stops replication, same as MariaDB.

 * `START SLAVE`

   * Starts replication, same as MariaDB.

 * `RESET SLAVE`

   * Resets replication. Note that the `RESET SLAVE ALL` form that is supported
     by MariaDB isn't supported by the binlogrouter.

 * `SHOW BINARY LOGS`

   * Lists the current files and their sizes. These will be different from the
     ones listed by the original master where the binlogrouter is replicating
     from.

 * `PURGE { BINARY | MASTER } LOGS TO <filename>`

   * Purges binary logs up to but not including the given file. The file name
     must be one of the names shown in `SHOW BINARY LOGS`. The version of this
     command which accepts a timestamp is not currently supported.
     Automatic purging is supported using the configuration
     parameter [`expire_log_duration`](#expire_log_duration).

     The files are purged in the order they were created. If a file to be purged
     is detected to be in use, the purge stops. This means that the purge will
     stop at the oldest file that a slave is still reading.

     **NOTE:**: You should still take precaution not to purge files that a potential
     slave will need in the future. MaxScale can only detect that a file is
     in active use when a slave is connected, and requesting events from it.

 * `SHOW MASTER STATUS`

   * Shows the current binlog and position in that file where the binlogrouter
     is writing the replicated data. These file names will be different from the
     ones shown by the original master.

 * `SHOW SLAVE STATUS`

   * Shows the slave status information similar to what a normal MariaDB slave
     server shows. Some of the values are replaced with constants values that
     never change. The following values are not constant:

     * `Slave_IO_State`: Set to `Waiting for master to send event` when
       replication is ongoing.

     * `Master_Host`: Address of the current master.

     * `Master_User`: The user used to replicate.

     * `Master_Port`: The port the master is listening on.

     * `Master_Log_File`: The name of the latest file that the binlogrouter is
       writing to.

     * `Read_Master_Log_Pos`: The current position where the last event was
       written in the latest binlog.

     * `Slave_IO_Running`: Set to `Yes` if replication running and `No` if it's
       not.

     * `Slave_SQL_Running` Set to `Yes` if replication running and `No` if it's
       not.

     * `Exec_Master_Log_Pos`: Same as `Read_Master_Log_Pos`.

     * `Gtid_IO_Pos`: The latest replicated GTID.

 * `SELECT { Field } ...`

   * The binlogrouter implements a small subset of the MariaDB SELECT syntax as
     it is mainly used by the replicating slaves to query various parameters. If
     a field queried by a client is not known to the binlogrouter, the value
     will be returned back as-is. The following list of functions and variables
     are understood by the binlogrouter and are replaced with actual values:

     * `@@gtid_slave_pos`, `@@gtid_current_pos` or `@@gtid_binlog_pos`: All of
       these return the latest GTID replicated from the master.

     * `version()` or `@@version`: The version string returned by MaxScale when
       a client connects to it.

     * `UNIX_TIMESTAMP()`: The current timestamp.

     * `@@version_comment`: Always `pinloki`.

     * `@@global.gtid_domain_id`: Always `0`.

     * `@master_binlog_checksum`: Always `CRC32`.

 * `SET`

   * This is only for replication purposes and should not be used by actual
     clients.

## Configuration Parameters

The binlogrouter is configured similarly to how normal routers are configured in
MaxScale. It requires at least one listener where clients can connect to and one
server from which the database user information can be retrieved. An example
configuration can be found in the [example](#example) section of this document.

### `datadir`

Directory where binary log files are stored. By default the files are stored in
`/var/lib/maxscale/binlogs`. **NOTE:** If you are upgrading from a version prior
to 2.5, make sure this directory is different from what it was before, or
move the old data.

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

When this feature is enabled, the master which binlogrouter will replicate
from will be selected from the servers defined by a monitor `cluster=TheMonitor`.
Alternatively servers can be listed in `servers`. The servers should be monitored
by a monitor. Only servers with the `Master` status are used. If multiple master
servers are available, the first available master server will be used.

If a `CHANGE MASTER TO` command is received while `select_master` is on, the
command will be honored and `select_master` turned off until the next reboot.
This allows the Monitor to perform failover, and more importantly, switchover.
It also allows the user to manually redirect the Binlogrouter. The current
master is "sticky", meaning that the same master will be chosen on reboot.

**NOTE:** Do not use `auto_rejoin`. This restriction will be lifted in
a future version.

The GTID the replication will start from will be based on the latest replicated
GTID. If no GTID has been replicated, the router will start replication from the
start. Manual configuration of the GTID can be done by first configuring the
replication manually with `CHANGE MASTER TO`.

### `expire_log_duration`

Duration after which a binary log file can be automatically removed. The default is 0,
or no automatic removal. This is similar to the [Server system variable
expire_log_days](https://mariadb.com/kb/en/replication-and-binary-log-system-variables/#expire_logs_days).

The duration is measured from the last modification of the log file. Files are
purged in the order they were created. The automatic purge works in a similar
manner to `PURGE BINARY LOGS TO <filename>` in that it will stop the purge if
an eligible file is in active use, i.e. being read by a slave.

The duration can be specified as explained
[here](../Getting-Started/Configuration-Guide.md#durations).

### `expire_log_minimum_files`

The minimum number of log files the automatic purge keeps. At least one file
is always kept. The default setting is 2.

## New installation

 1. Configure and start MaxScale

 1. If you have not configured `select_master=true` (automatic
    master selection), issue a `CHANGE MASTER TO` command to binlogrouter.

    ```
    mysql -u USER -pPASSWORD -h maxscale-IP -P binlog-PORT

    CHANGE MASTER TO master_host="master-IP", master_port=master-PORT,
    master_user=USER, master_password="PASSWORD",
    master_use_gtid=slave_pos;
    START SLAVE;
    ```

 1. Redirect each slave to replicate from Binlogrouter

    ```
    mysql -u USER -pPASSWORD -h slave-IP -P slave-PORT

    STOP SLAVE;
    CHANGE MASTER TO master_host="maxscale-IP", master_port=binlog-PORT,
    master_user="USER", master_password="PASSWORD",
    master_use_gtid=slave_pos;
    START SLAVE;
    SHOW SLAVE STATUS \G
    ```

## Upgrading to version 2.5

Binlogrouter does not read any of the data that a version prior to 2.5
has saved. By default binlogrouter will request the replication stream
from the blank state (from the start of time), which is basically meant
for new systems. If a system is live, the entire replication data probably
does not exist, and if it does, it is not necessary for binlogrouter to read
and store all the data.

### Before you start

 * Note that binlogrouter only supports GTID based replication.
 * Make sure that the configured data directory for the new binlogrouter
   is different from the old one, or move old data away.
   See [datadir](#datadir).
 * If the master contains binlogs from the blank state, and there
   is a large amount of data, consider purging old binlogs.
   See [Using and Maintaining the Binary Log](https://mariadb.com/kb/en/using-and-maintaining-the-binary-log/).

### Deployment

The method described here inflicts the least downtime. Assuming you have
configured version 2.5, and it is ready to go:

 1. Redirect each slave that replicates from Binlogrouter to replicate from the
    master.

    ```
    mysql -u USER -pPASSWORD -h slave-IP -P slave-PORT

    STOP SLAVE;
    CHANGE MASTER TO master_host="master-IP", master_port=master-PORT,
    master_user="USER", master_password="PASSWORD",
    master_use_gtid=slave_pos;
    START SLAVE;
    SHOW SLAVE STATUS \G
    ```

 1. Stop the old version of MaxScale, and start the new one.
    Verify routing functionality.

 1. If you have not configured `select_master=true` (automatic
    master selection), issue a `CHANGE MASTER TO` command.

    ```
    mysql -u USER -pPASSWORD -h maxscale-IP -P binlog-PORT

    CHANGE MASTER TO master_host="master-IP", master_port=master-PORT,
    master_user=USER,master_password="PASSWORD",
    master_use_gtid=slave_pos;

    ```

 1. Run `maxctrl list servers`. Make sure all your servers are accounted for.
    Pick the lowest gtid on display and issue this command
    to Binlogrouter:

    ```
    STOP SLAVE
    SET @@global.gtid_slave_pos = "lowest-GTID";
    START SLAVE
    ```

 1. Redirect each slave to replicate from Binlogrouter

   ```
   mysql -u USER -pPASSWORD -h slave-IP -P slave-PORT

   STOP SLAVE;
   CHANGE MASTER TO master_host="maxscale-IP", master_port=binlog-PORT,
   master_user="USER", master_password="PASSWORD",
   master_use_gtid=slave_pos;
   START SLAVE;
   SHOW SLAVE STATUS \G
   ```

## Example

The following is a minimal configuration with automatic master selection. With it, the
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
cluster=MariaDB-Monitor
select_master=true
expire_log_duration=5h
expire_log_minimum_files=3
user=maxuser
password=maxpwd

[Replication-Listener]
type=listener
service=Replication-Proxy
protocol=MariaDBClient
port=3306
```

## Limitations

* Old-style replication with binlog name and file offset is not supported
  and the replication must be started by setting up the GTID to replicate
  from.

* Only replication from MariaDB servers is supported.
