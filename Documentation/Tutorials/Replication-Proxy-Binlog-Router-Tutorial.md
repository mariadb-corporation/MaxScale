# MariaDB MaxScale as a Binlog Server

# Table of Contents

* [Introduction](#introduction)
  * [MariaDB as a Binlog Server](#mariadb-as-a-binlog-server)
  * [MariaDB MaxScale's approach](#mariadb-maxscales-approach)
* [Configuring MariaDB MaxScale as a Binlog Server](#configuring-mariadb-maxscale-as-a-binlog-server)
  * [Service Configuration](#service-configuration)
  * [Listener Configuration](#listener-configuration)
  * [Configuring Replication](#configuring-replication)
* [Binlog Router Compatibility](#binlog-router-compatibility)
* [MariaDB MaxScale Replication Diagnostics](#mariadb-maxscale-replication-diagnostics)

# Introduction

MariaDB MaxScale is a dynamic data routing platform that sits between a database
layer and the clients of that database. However, the binlog router described
here is somewhat different to that original concept, moving MariaDB MaxScale
down to play a role within the database layer itself.

In a traditional MariaDB replication setup a single master server is created and
a set of slaves MariaDB instances are configured to pull the binlog files from
that master to the slaves. There are some problems, however, in this setup; when
the number of slaves grows, an increasing load caused by the serving of binlogs
to each slave, is placed on the master. When the master server fails, some
action must be performed on every slave server before a new server can become
the master server.

Introducing a proxy layer between the master server and the slave servers can
improve the situation, by reducing the load on the master to simply serving the
proxy layer rather than all of the slaves. The slaves only need to be aware of
the proxy layer and not of the real master server. Removing the need for the
slaves to have knowledge of the actual master, greatly simplifies the process of
replacing a failed master within a replication environment.

## MariaDB as a Binlog Server

The most obvious solution to the requirement for a proxy layer within a
replication environment is to use a MariaDB or MySQL database instance. The
database server is designed to allow this, since a slave server is able to be
configured such that it will produce binary logs for updates it has itself
received via replication from the master server. This is done with the
*log_slave_updates* configuration option of the server. In this case the server
is known as an intermediate master, it is simultaneously a slave to the real
master and a master to the other slaves in the configuration.

Using an intermediate master does not, however, solve all the problems and
introduces some new ones, due to the way replication is implemented. A slave
server reads the binary log data and creates a relay log from that binary
log. This log provides a source of SQL statements, which are executed within the
slave in order to make the same changes to the databases on the slaves as were
made on the master. If the *log_slave_updates* option has been enabled, new
binary log entries are created for the statements executed from the relay log.

The above means that the data in the binary log of the intermediate master is
not a direct copy of the data that was received from the binary log of the real
master. The resultant changes to the database will be the same, provided no
updates have been performed on the intermediate master that did not originate on
the real master, but the steps to achieve those changes may be different. In
particular, if group commit functionality is used, to allow multiple
transactions to commit in parallel, these may well be different on the
intermediate master. This can cause a reduction in the parallelism of the
commits and a subsequent reduction in the performance of the slave servers.

This re-execution of the SQL statements also adds latency to the intermediate
master solution, since the full process of parsing, optimization and execution
must occur for every statement that is replicated from the master to the slaves
must be performed in the intermediate master. This latency introduces lag in the
replication chain, with a greater delay being introduced from the time a
transaction is committed on the master until the data is available on the
slaves.

Use of an intermediate master does improve the process of failover of the master
server, since the slaves are only aware of the intermediate master the process
of promoting one of the existing slaves to become the new master only involves
that slave and the intermediate master. A slave can become the new master as
soon as all the changes from the intermediate master have been processed. The
intermediate master then needs to be reset to the correct point in the binary
log of the new master and replication can continue.

An added complexity that needs to be dealt with is the failure of the
intermediate master itself. If this occurs then the same problem as described
earlier exists, all slaves must be updated when a new intermediate master is
created. If multiple intermediate masters are used, there is also a restriction
that slaves can not be moved from the failed intermediate master to another
intermediate master due to the fact that the binlog on the different
intermediate nodes are not guaranteed to be the same.

## MariaDB MaxScale's approach

MariaDB MaxScale takes a much simpler approach to the process of being a Binlog
Server. It acts as a slave to the real master and as a master to the slaves, in
the same way as an intermediate master does. However, it does not implement any
re-execution of the statements within the binary log. MariaDB MaxScale creates a
local cache of the binary logs it receives from the master and will serve binary
log events to the slaves from this cache of the master's binary log. This means
that the slaves will always get binary log events that have a one-to-one
correlation to those written by the master. Parallelism in the binary log events
of the master is maintained in the events that are observed by the slaves.

In the MariaDB MaxScale approach, the latency that is introduced is mostly the
added network latency associated with adding the extra network hop. There is no
appreciable processing performed at the MariaDB MaxScale level, other than for
managing the local cache of the binlog files.

In addition, every MariaDB MaxScale that is acting as a proxy of the master will
have exactly the same binlog events as the master itself. This means that a
slave can be moved between any of the MariaDB MaxScale server or to the real
master without a need to perform any special processing. The result is much
simpler behavior for failure recovery and the ability to have a very simple,
redundant proxy layer with slaves free to both between the proxies.

# Configuring MariaDB MaxScale as a Binlog Server

Using MariaDB MaxScale as a Binlog Server is much the same as using MariaDB
MaxScale as a proxy between the clients and the database servers. In this case
the master server should be considered as the database backend and the slave
servers as the clients of MariaDB MaxScale.

## Service Configuration

As with any MariaDB MaxScale configuration a good starting point is with the
service definition with the *maxscale.cnf* file. The service requires a name
which is the section name in the ini file, a type parameter with a value of
service and the name of the router plugin that should be loaded. In the case of
replication proxies this router name is *binlogrouter*.

The minimum set of router options that must be given in the configuration are
are `server_id` and `binlogdir`, default values may be used for all other
options.

All configuration prameters can be found in the [Binlog Router
Documentation](../Routers/Binlogrouter.md).

A minimal example of a service entry for a binlog router service that is used
with MariaDB 10 would be as follows.

```
[Replication]
type=service
router=binlogrouter
user=maxscale
passwd=maxpwd
server_id=1
mariadb10-compatibility=1
binlogdir=/var/lib/maxscale/
```

## Listener Configuration

As per any service in MariaDB MaxScale, a listener section is required to define
the address, port and protocol that is used to listen for incoming
connections. Those incoming connections will originate from either slave servers
or from a MySQL client. The binlogrouter is administered and configured via SQL
commands on the listener.

```
[Replication Listener]
type=listener
service=Replication
protocol=MariaDBClient
port=3306
```

The protocol used by slaves for connection to MariaDB MaxScale is the same
*MariaDBClient* protocol that is used for client applications to connect to
databases, therefore the same MariaDB MaxScale protocol module can be used.

It's also possible to enable client side SSL by adding the required SSL options
in the listener:

```
[Replication SSL Listener]
type=listener
service=Replication
protocol=MariaDBClient
port=3306
ssl=required
ssl_key=/path/to/key.pem
ssl_cert=/path/to/cert.pem
ssl_ca_cert=/path/to/ca-cert.pem
```

Refer to the [Configuration-Guide](../Getting-Started/Configuration-Guide.md)
for more details about the SSL configuration in MaxScale.

## Configuring Replication

When the binlogrouter is started for the first time, it needs to be configured
to replicate from a master. To do this, connect to the binlogrouter listener
that was defined before and execute a normal `CHANGE MASTER TO` command. Use the
credentials defined in `maxscale.cnf` when you connect to MaxScale. Finally,
execute a `START SLAVE` command to start the replication.

Here is an example SQL command that configures the binlogrouter to replicate
from a MariaDB server and starts replication:

```
CHANGE MASTER TO MASTER_HOST='master.example.com',
                 MASTER_PORT=3306,
                 MASTER_USER='maxuser',
                 MASTER_PASSWORD='maxpwd',
                 MASTER_LOG_FILE='mysql-bin.000001',
                 MASTER_LOG_POS=4;

START SLAVE;
```

Both the _MASTER_LOG_FILE_ and _MASTER_LOG_POS_ must be defined and the value of
_MASTER_LOG_POS_ must be 4.

**Note:** Legacy versions defined the server by configuring a separate server
object in `maxscale.cnf`.

### Stopping and Starting the Replication

When router is configured and it is properly working it is possible to stop the
replication with `STOP SLAVE` and to resume it with `START SLAVE`. In addition
to this, the `SHOW SLAVE STATUS` command can be used to display information
about the replication configuration.

Slave connections are not affected by the `STOP SLAVE` and `START SLAVE`
commands. They only control the connection to the master server.

### Change the Master server configuration

When router is configured and it is properly working it is possible to change the master parameters.
First step is stop the replication from the master.

```
STOP SLAVE;
```

Next step is the master configuration

```
CHANGE MASTER TO ...
```

A successful configuration change results in *master.ini* being updated. Any
error is reported in the MySQL and in log files.

The supported `CHAGE MASTER TO` options are:

- `MASTER_HOST`
- `MASTER_PORT`
- `MASTER_USER`
- `MASTER_PASSWORD`
- `MASTER_LOG_FILE`
- `MASTER_LOG_POS`
- `MASTER_SSL`
- `MASTER_SSL_CERT` (path to certificate file)
- `MASTER_SSL_KEY` (path to key file)
- `MASTER_SSL_CA` (path to CA cerificate file)
- `MASTER_TLS_VERSION` (TLS/SSL version)

Further details about level of encryption or certificates could be found in the
[Configuration Guide](../Getting-Started/Configuration-Guide.md)

### Slave servers setup

Examples of *CHANGE MASTER TO* command issued on a slave server that wants to
gets replication events from MariaDB MaxScale binlog router:

```
CHANGE MASTER TO MASTER_HOST=‘$maxscale_IP’, MASTER_PORT=5308, MASTER_USER='repl', MASTER_PASSWORD=‘somepasswd’,
MASTER_LOG_FILE=‘mysql-bin.000001'

CHANGE MASTER TO MASTER_HOST=‘$maxscale_IP’, MASTER_PORT=5308, MASTER_USER='repl', MASTER_PASSWORD=‘somepasswd’,
MASTER_LOG_FILE=‘mysql-bin.000159', MASTER_LOG_POS=245
```

The latter example specifies a *MASTER_LOG_POS* for the selected
*MASTER_LOG_FILE*

**Note:**

 - *MASTER_LOG_FILE* must be set to one of existing binlog files in MariaDB
    MaxScale binlogdir

 - If *MASTER_LOG_POS* is not set with *CHANGE MASTER TO* it defaults to 4

 - Latest binlog file name and pos in MariaDB MaxScale can be found by executing
   `SHOW MASTER STATUS` on MaxScale.

### Controlling the Binlogrouter

There are some constraints related to *MASTER_LOG_FILE* and *MASTER_LOG_POS*.
*MASTER_LOG_FILE* can be changed to next binlog in sequence with
*MASTER_LOG_POS=4* or to current one at current position.

Examples:

1) Current binlog file is ‘mysql-bin.000003', position 88888

    MariaDB> CHANGE MASTER TO MASTER_LOG_FILE=‘mysql-bin.000003',MASTER_LOG_POS=8888

This could be applied to current master_host/port or a new one.  If there is a
master server maintenance and a slave is being promoted as master it should be
checked that binlog file and position are valid: in case of any error
replication stops and errors are reported via *SHOW SLAVE STATUS* and in error
logs.

2) Current binlog file is ‘mysql-bin.000099', position 1234

    MariaDB> CHANGE MASTER TO MASTER_LOG_FILE=‘mysql-bin.000100',MASTER_LOG_POS=4

This could be applied with current master_host/port or a new one If transaction
safety option is on and the current binlog file contains an incomplete
transaction it will be truncated to the position where transaction started. In
such situation a proper message is reported in MySQL connection and with next
START SLAVE binlog file truncation will occur and MariaDB MaxScale will request
events from the master using the next binlog file at position 4.

The above scenario might refer to a master crash/failure: the new server that
has just been promoted as master doesn't have last transaction events but it
should have the new binlog file (the next in sequence). Truncating the previous
MariaDB MaxScale binlog is safe as that incomplete transaction is lost. It
should be checked that current master or new one has the new binlog file, in
case of any error replication stops and errors are reported via *SHOW SLAVE
STATUS* and in error logs.

    MariaDB> START SLAVE;

Check for any error in log files with:

    MariaDB> SHOW SLAVE STATUS;

In some situations replication state could be *STOPPED* and proper messages are
displayed in error logs and in *SHOW SLAVE STATUS*. In order to resolve any
mistake done with *CHANGE MASTER TO MASTER_LOG_FILE / MASTER_LOG_POS*, another
administrative command can be helpful.

    MariaDB> RESET SLAVE;

This command removes *master.ini* file, blanks all master configuration in
memory and sets binlog router in unconfigured state: a *CHANGE MASTER TO*
command should be issued for the new configuration.

**Note:** existing binlog files are not touched by this command.

Examples with SSL options:

    MySQL [(none)]> CHANGE MASTER TO MASTER_SSL = 1, MASTER_SSL_CERT='/home/maxscale/packages/certificates/client/client-cert.pem', MASTER_SSL_CA='/home/maxscale/packages/certificates/client/ca.pem', MASTER_SSL_KEY='/home/maxscale/packages/certificates/client/client-key.pem', MASTER_TLS_VERSION='TLSv12';

    MySQL [(none)]> CHANGE MASTER TO MASTER_TLS_VERSION='TLSv12';

    MySQL [(none)]> CHANGE MASTER TO MASTER_SSL = 0;


#### SSL Limitations

 - In order to enable/re-enable Master SSL comunication the MASTER_SSL=1 option
   is required and all certificate options must be explicitey set in the same
   CHANGE MASTER TO command.

 - New certificate options changes take effect after maxScale restart or after
   MASTER_SSL=1 with the new options.

 - SHOW SLAVE STATUS displays all the options but MASTER_TLS_VERSION value.

 - Maxadmin, 'show services' or 'show service $binlog_service' displays all the
   options when SSL is on.

 - STOP SLAVE is required for CHANGE MASTER TO command (any option)

 - START SLAVE will use new SSL options for Master SSL communication setup.

# Binlog Router Compatibility

Binlog Router Plugin is compatible with MariaDB 5.5, 10.0, 10.1 and 10.2 as well
as MySQL 5.6 and 5.7.

Note: When using MariaDB 10.2 or MySQL 5.7 the `send_slave_heartbeat` option
must be set to On as the slave servers request the hearbeat to MaxScale.
As an alternative use `CHANGE MASTER TO MASTER_HEARTBEAT_PERIOD=0` in
the slave server in order to disable the heartbeat request.

## Enabling MariaDB 10 compatibility

MariaDB 10 has different slave registration phase so an extra option is required:

```
mariadb10-compatibility=1
```

`version_string` can be modified in order to present MariaDB 10 version when
MariaDB MaxScale sends server handshake packet.

```
version_string=10.0.17-log
```

## MySQL Limitations

In order to use it with MySQL 5.6/5.7, the *GTID_MODE* setting must be OFF and
connecting slaves must not use *MASTER_AUTO_POSITION = 1* option. Additionally
with MySQL 5.7 slaves the `send_slave_heartbeat` option must be set to on.

Binlog Router currently does not work for MySQL 5.5 due to missing
*@@global.binlog_checksum* variable.

## MariaDB Limitations
Starting from version 10.2 there are new replication events related
to binlog event compression: these new events are not supported yet.
Be sure that `log_bin_compress` is not set in any MariaDB 10.2 server.

#  MariaDB MaxScale Replication Diagnostics

The binlog router module of MariaDB MaxScale produces diagnostic output that can
be viewed via the `maxadmin` client application. Running the maxadmin command
and issuing a show service command will produce output that will show both the
master connection status and statistics and also a block for each of the slaves
currently connected.

```
-bash-4.1$ maxadmin show service Replication
    Service 0x1567ef0
        Service:                Replication
        Router:                 binlogrouter (0x7f4ceb96a820)
        State:                  Started
        Master connection DCB:                      0x15693c0
        Master connection state:                    Binlog Dump
        Binlog directory:                           /var/maxscale/binlogs
        Heartbeat period (seconds):                 200
        Number of master connects:                  1
        Number of delayed reconnects:               0
        Current binlog file:                        mybin.000061
        Current binlog position:                    120
        Number of slave servers:                    0
        No. of binlog events received this session: 1002705
        Total no. of binlog events received:        2005410
        No. of bad CRC received from master:        0
        Number of binlog events per minute
        Current        5        10       15       30 Min Avg
              4       4.0      4.0      4.0      4.0
        Number of fake binlog events:           0
        Number of artificial binlog events:     61
        Number of binlog events in error:       0
        Number of binlog rotate events:         60
        Number of heartbeat events:             69
        Number of packets received:             599
        Number of residual data packets:        379
        Average events per packet               3347.9
        Last event from master at:              Thu Jan 29 16:41:53 2015 (10 seconds ago)
        Last event from master:                 0x1b (Heartbeat Event)
        Events received:
            Invalid                                  0
            Start Event V3                           0
            Query Event                              703307
            Stop Event                               55
            Rotate Event                             65
            Integer Session Variable                 0
            Load Event                               0
            Slave Event                              0
            Create File Event                        0
            Append Block Event                       0
            Exec Load Event                          0
            Delete File Event                        0
            New Load Event                           0
            Rand Event                               0
            User Variable Event                      0
            Format Description Event                 61
            Transaction ID Event (2 Phase Commit)    299148
            Begin Load Query Event                   0
            Execute Load Query Event                 0
            Table Map Event                          0
            Write Rows Event (v0)                    0
            Update Rows Event (v0)                   0
            Delete Rows Event (v0)                   0
            Write Rows Event (v1)                    0
            Update Rows Event (v1)                   0
            Delete Rows Event (v1)                   0
            Incident Event                           0
            Heartbeat Event                          69
            Ignorable Event                          0
            Rows Query Event                         0
            Write Rows Event (v2)                    0
            Update Rows Event (v2)                   0
            Delete Rows Event (v2)                   0
            GTID Event                               0
            Anonymous GTID Event                     0
            Previous GTIDS Event                     0
        Started:                Thu Jan 29 16:06:11 2015
        Root user access:           Disabled
        Backend databases
            178.62.50.70:3306  Protocol: MariaDBBackend
        Users data:                     0x156c030
        Total connections:              2
        Currently connected:            2
```

If a slave is connected to MaxScale with SSL, an entry will be present in the
Slave report:

```
    Slaves:
        Server-id:                               106
        Hostname:                                SBslave6
        Slave UUID:                              00019686-7777-7777-7777-777777777777
        Slave_host_port:                         188.165.213.5:40365
        Username:                                massi
        Slave DCB:                               0x7fc01be3ba88
        Slave connected with SSL:                Established

```

The `SHOW SLAVE STATUS` command provides diagnostic information about the
replication state.

```
MySQL [(none)]> show slave status\G
*************************** 1. row ***************************
               Slave_IO_State: Binlog Dump
                  Master_Host: 88.26.197.94
                  Master_User: repl
                  Master_Port: 3306
                Connect_Retry: 60
              Master_Log_File: mysql-bin.003140
          Read_Master_Log_Pos: 16682679
               Relay_Log_File: mysql-bin.003140
                Relay_Log_Pos: 16682679
        Relay_Master_Log_File: mysql-bin.003140
             Slave_IO_Running: Yes
            Slave_SQL_Running: Yes
              Replicate_Do_DB:
          Replicate_Ignore_DB:
           Replicate_Do_Table:
       Replicate_Ignore_Table:
      Replicate_Wild_Do_Table:
  Replicate_Wild_Ignore_Table:
                   Last_Errno: 0
                   Last_Error:
                 Skip_Counter: 0
          Exec_Master_Log_Pos: 16682679
              Relay_Log_Space: 16682679
              Until_Condition: None
               Until_Log_File:
                Until_Log_Pos: 0
           Master_SSL_Allowed: Yes
           Master_SSL_CA_File: /home/maxscale/packages/certificates/client/ca.pem
           Master_SSL_CA_Path:
              Master_SSL_Cert: /home/maxscale/packages/certificates/client/client-cert.pem
            Master_SSL_Cipher:
               Master_SSL_Key: /home/maxscale/packages/certificates/client/client-key.pem
        Seconds_Behind_Master: 0
Master_SSL_Verify_Server_Cert: No
                Last_IO_Errno: 0
                Last_IO_Error:
               Last_SQL_Errno: 0
               Last_SQL_Error:
  Replicate_Ignore_Server_Ids:
             Master_Server_Id: 1111
                  Master_UUID: 6aae714e-b975-11e3-bc33-0401152c3d01
             Master_Info_File: /home/maxscale/binlog/first/binlogs/master.ini
```

MariaDB 10 masters display some extra events.

```
MariaDB 10 Annotate Rows Event 0
MariaDB 10 Binlog Checkpoint Event 0
MariaDB 10 GTID Event 0
MariaDB 10 GTID List Event 0
```
