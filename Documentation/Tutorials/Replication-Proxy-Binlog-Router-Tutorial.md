# MariaDB MaxScale as a Binlog Server
MariaDB MaxScale is a dynamic data routing platform that sits between a database layer and the clients of that database. However, the binlog router described here is somewhat different to that original concept, moving MariaDB MaxScale down to play a role within the database layer itself.

In a traditional MariaDB replication setup a single master server is created and a set of slaves MariaDB instances are configured to pull the binlog files from that master to the slaves. There are some problems, however, in this setup; when the number of slaves grows, an increasing load caused by the serving of binlogs to each slave, is placed on the master. When the master server fails, some action must be performed on every slave server before a new server can become the master server.

Introducing a proxy layer between the master server and the slave servers can improve the situation, by reducing the load on the master to simply serving the proxy layer rather than all of the slaves. The slaves only need to be aware of the proxy layer and not of the real master server. Removing the need for the slaves to have knowledge of the actual master, greatly simplifies the process of replacing a failed master within a replication environment.

## MariaDB as a Binlog Server
The most obvious solution to the requirement for a proxy layer within a replication environment is to use a MariaDB or MySQL database instance. The database server is designed to allow this, since a slave server is able to be configured such that it will produce binary logs for updates it has itself received via replication from the master server. This is done with the *log_slave_updates* configuration option of the server. In this case the server is known as an intermediate master, it is simultaneously a slave to the real master and a master to the other slaves in the configuration.

Using an intermediate master does not, however, solve all the problems and introduces some new ones, due to the way replication is implemented. A slave server reads the binary log data and creates a relay log from that binary log. This log provides a source of SQL statements, which are executed within the slave in order to make the same changes to the databases on the slaves as were made on the master. If the *log_slave_updates* option has been enabled, new binary log entries are created for the statements executed from the relay log.

The above means that the data in the binary log of the intermediate master is not a direct copy of the data that was received from the binary log of the real master. The resultant changes to the database will be the same, provided no updates have been performed on the intermediate master that did not originate on the real master, but the steps to achieve those changes may be different. In particular, if group commit functionality is used, to allow multiple transactions to commit in parallel, these may well be different on the intermediate master. This can cause a reduction in the parallelism of the commits and a subsequent reduction in the performance of the slave servers.

This re-execution of the SQL statements also adds latency to the intermediate master solution, since the full process of parsing, optimization and execution must occur for every statement that is replicated from the master to the slaves must be performed in the intermediate master. This latency introduces lag in the replication chain, with a greater delay being introduced from the time a transaction is committed on the master until the data is available on the slaves.

Use of an intermediate master does improve the process of failover of the master server, since the slaves are only aware of the intermediate master the process of promoting one of the existing slaves to become the new master only involves that slave and the intermediate master. A slave can become the new master as soon as all the changes from the intermediate master have been processed. The intermediate master then needs to be reset to the correct point in the binary log of the new master and replication can continue.

An added complexity that needs to be dealt with is the failure of the intermediate master itself. If this occurs then the same problem as described earlier exists, all slaves must be updated when a new intermediate master is created. If multiple intermediate masters are used, there is also a restriction that slaves can not be moved from the failed intermediate master to another intermediate master due to the fact that the binlog on the different intermediate nodes are not guaranteed to be the same.

## MariaDB MaxScale's approach
MariaDB MaxScale takes a much simpler approach to the process of being a Binlog Server. It acts as a slave to the real master and as a master to the slaves, in the same way as an intermediate master does. However, it does not implement any re-execution of the statements within the binary log. MariaDB MaxScale creates a local cache of the binary logs it receives from the master and will serve binary log events to the slaves from this cache of the master's binary log. This means that the slaves will always get binary log events that have a one-to-one correlation to those written by the master. Parallelism in the binary log events of the master is maintained in the events that are observed by the slaves.

In the MariaDB MaxScale approach, the latency that is introduced is mostly the added network latency associated with adding the extra network hop. There is no appreciable processing performed at the MariaDB MaxScale level, other than for managing the local cache of the binlog files.

In addition, every MariaDB MaxScale that is acting as a proxy of the master will have exactly the same binlog events as the master itself. This means that a slave can be moved between any of the MariaDB MaxScale server or to the real master without a need to perform any special processing. The result is much simpler behavior for failure recovery and the ability to have a very simple, redundant proxy layer with slaves free to both between the proxies.

# Configuring MariaDB MaxScale as a Binlog Server
Using MariaDB MaxScale as a Binlog Server is much the same as using MariaDB MaxScale as a proxy between the clients and the database servers. In this case the master server should be considered as the database backend and the slave servers as the clients of MariaDB MaxScale.

## Service Configuration

As with any MariaDB MaxScale configuration a good starting point is with the service definition with the *maxscale.cnf* file. The service requires a name which is the section name in the ini file, a type parameter with a value of service and the name of the router plugin that should be loaded. In the case of replication proxies this router name is *binlogrouter*.


The minimum set of router options that must be given in the configuration are are *server-id* and *master-id*, default values may be used for all other options.

Additional information about the encryption of the Binlog files can be found here:
[Binlogrouter - The replication protocol proxy module for MariaDB MaxScale](../Routers/Binlogrouter.md).


A **complete example** of a service entry for a binlog router service would be as follows.

```
    [Replication]
    type=service
    router=binlogrouter
    version_string=5.6.17-log
    user=maxscale
    passwd=Mhu87p2D
    router_options=uuid=f12fcb7f-b97b-11e3-bc5e-0401152c4c22,
                   server-id=3,
                   user=repl,
                   password=slavepass,
                   master-id=1,
                   heartbeat=30,
                   binlogdir=/var/binlogs,
                   transaction_safety=1,
                   master_version=5.6.19-common,
                   master_hostname=common_server,
                   master_uuid=x-f-cc-common,
                   master-id=999,
                   mariadb10-compatibility=On,
                   ssl_cert_verification_depth=9,
                   semisync=On,
                   encrypt_binlog=On,
                   encryption_algorithm=aes_ctr,
                   encryption_key_file=/var/binlogs/enc_key.txt,
                   mariadb10_slave_gtid=On,
                   mariadb10_master_gtid=Off,
                   slave_hostname=maxscale-blr-1,
                   master_retry_count=1000,
                   connect_retry=60
```

The minimum set of router options that must be given in the configuration
are *server-id* and *master-id*, default values may be used for all other options.

## Listener Section

As per any service in MariaDB MaxScale a listener section is required to define the address, port and protocol that is used to listen for incoming connections. Those incoming connections will originate from the slave servers or from a MySQL client in order to administer/configure the master server configuration via SQL commands such as *STOP/START SLAVE* and *CHANGE MASTER TO* ...

    [Replication Listener]
    type=listener
    service=Replication
    protocol=MySQLClient
    port=5308

The protocol used by slaves for connection to MariaDB MaxScale is the same *MySQLClient* protocol that is  used for client applications to connect to databases, therefore the same MariaDB MaxScale protocol module can be used.

It's also possible to enable SSL from clients (MySQL clients or Slave servers) by adding SSL options in the listener, or in a new one:
```
   [Replication Listener_SSL]
   type=listener
   service=Replication
   protocol=MySQLClient
   port=5309
   ssl=required
   ssl_key=/path_to/key.pem
   ssl_cert=/path_to/cert.pem
   ssl_ca_cert=/path_to/ca-cert.pem
   #ssl_version=TLSv10
```
Check the [Configuration-Guide](../Getting-Started/Configuration-Guide.md) for SSL options details.

#  MariaDB MaxScale replication diagnostics

The binlog router module of MariaDB MaxScale produces diagnostic output that can be viewed via the `maxadmin` client application. Running the maxadmin command and issuing a show service command will produce a considerable amount of output that will show both the master connection status and statistics and also a block for each of the slaves currently connected.


```
-bash-4.1$ maxadmin show service Replication
    Service 0x1567ef0
    	Service:				Replication
    	Router: 				binlogrouter (0x7f4ceb96a820)
    	State: 					Started
    	Master connection DCB:  					0x15693c0
    	Master connection state:					Binlog Dump
    	Binlog directory:						    /var/maxscale/binlogs
        Heartbeat period (seconds):					200
    	Number of master connects:		  			1
    	Number of delayed reconnects:	      		0
    	Current binlog file:		  				mybin.000061
    	Current binlog position:	  				120
    	Number of slave servers:	   				0
    	No. of binlog events received this session:	1002705
    	Total no. of binlog events received:        2005410
    	No. of bad CRC received from master:       	0
    	Number of binlog events per minute
    	Current        5        10       15       30 Min Avg
    	      4       4.0      4.0      4.0      4.0
    	Number of fake binlog events:      		0
    	Number of artificial binlog events: 	61
    	Number of binlog events in error:  		0
    	Number of binlog rotate events:  		60
    	Number of heartbeat events:     		69
    	Number of packets received:				599
    	Number of residual data packets:		379
    	Average events per packet			    3347.9
    	Last event from master at:  			Thu Jan 29 16:41:53 2015 (10 seconds ago)
    	Last event from master:  				0x1b (Heartbeat Event)
    	Events received:
    		Invalid                              	 0
    		Start Event V3                       	 0
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
    	Started:				Thu Jan 29 16:06:11 2015
    	Root user access:			Disabled
    	Backend databases
    		178.62.50.70:3306  Protocol: MySQLBackend
    	Users data:        				0x156c030
    	Total connections:				2
    	Currently connected:			2
```

If a slave is connected to MaxScale with SSL, an entry will be present in the Slave report:

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

If option `mariadb10_slave_gtid=On` last seen GTID is shown:

```
Last seen MariaDB GTID:                      0-10124-282
```

Another Binlog Server diagnostic output comes from SHOW SLAVE STATUS MySQL command

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

If the option `mariadb10_slave_gtid` is set to On, the last seen GTID is shown:

```
Using_Gtid: No
Gtid_IO_Pos: 0-10116-196
```

If the option `mariadb10_master_gtid` is set to On, the _Using_Gtid_
field has the _Slave_pos_ value:

```
Using_Gtid: Slave_pos
Gtid_IO_Pos: 0-10116-196
```

# Binlog router compatibility

Binlog Router Plugin is compatible with MariaDB 5.5 and MySQL 5.6, the current default.

In order to use it with MySQL 5.6, the *GTID_MODE* setting must be OFF and connecting
slaves must not use *MASTER_AUTO_POSITION = 1* option.

It also works with a MariaDB 10.X setup (master and slaves).

Starting from MaxScale 2.2 the slave connections may include **GTID** feature
`MASTER_USE_GTID=Slave_pos` if option *mariadb10_slave_gtid* has been set.

The default is that a slave connection must not include any GTID
feature: `MASTER_USE_GTID=no`

Starting from MaxScale 2.2 it's also possible to register to MariaDB 10.X master using
**GTID** using the new option *mariadb10_master_gtid*.

Current GTID implementation limitations:

- It's not possible to specify the GTID _domain_id: the master one is being used for
all operations. All slave servers must use the same replication domain as the master server.
- One GTID value in GTID_LIST event received from Master
- One GTID value from connecting slave server.
- One GTID value for Master registration.

**Note:** Binlog Router currently does not work for MySQL 5.5 due to
missing *@@global.binlog_checksum* variable.

# Master server setup/change

In the MariaDB MaxScale ini file the server section for master is no longer required, same for *servers=master_server* in the service section. The master server setup is currently managed via *CHANGE MASTER TO* command issued in MySQL client connection to MariaDB MaxScale or by providing a proper *master.ini* file in the *binlogdir*.

If MariaDB MaxScale starts without *master.ini* there is no replication configured to any master and slaves cannot register to the router: the binlog router could be later configured via *CHANGE MASTER TO* and the *master.ini* file will be written.

Please note that is such condition the only user for MySQL protocol connection to MaxScale Binlog Server is the service user.

*master.ini* file example:

	[binlog_configuration]
	master_host=127.0.0.1
	master_port=3308
	master_user=repl
	master_password=somepass
	filestem=repl-bin
	# Master SSL communication options
	master_ssl=0
	master_ssl_key=/home/mpinto/packages/certificates/client/client-key.pem
	master_ssl_cert=/home/mpinto/packages/certificates/client/client-cert.pem
	master_ssl_ca=/home/mpinto/packages/certificates/client/ca.pem
	#master_tls_version=TLSv12
	#master_heartbeat_period=300
	#master_connect_retry=60

Enabling replication from a master server requires:

	CHANGE MASTER TO MASTER_HOST=‘$master_server’,
	MASTER_PORT=$master_port,
	MASTER_USER='repl',
	MASTER_PASSWORD=‘somepasswd’,
	MASTER_LOG_FILE=‘repl-bin.000159',
	MASTER_LOG_POS=4;

It's possible to specify the desired *MASTER_LOG_FILE* but position must be 4

**Note:** Since MaxScale 1.3.0, the _initfile_ and _filestem_ options are no longer
required, as the needed values are automatically set by parsing *MASTER_LOG_FILE* in CHANGE MASTER TO command.

##### MariaDB 10 GTID

Since MaxScale 2.2, if option _mariadb10_master_gtid_ is On, it's possible to use GTID (MASTER_USE_GTID=Slave_pos),
instead of _file_ and _pos_.
This also implies that MariaDB 10 slave servers can only connect with GTID mode to MaxScale.

```
MariaDB> SET @@global.gtid_slave_pos='0-198-123';
MariaDB> CHANGE MASTER TO
         MASTER_HOST=‘$master_server’,
         MASTER_PORT=$master_port,
         MASTER_USER='repl',
         MASTER_PASSWORD=‘somepasswd’,
         MASTER_USE_GTID=Slave_pos;
```
**Note**: the _log file name_ to write binlog events into is the one specified in
the _Fake Rotate_ event received at registration time.

### Stop/start the replication

When router is configured and it is properly working it is possible to stop/start replication:

	MariaDB> STOP SLAVE;
	...
	MariaDB> SHOW SLAVE STATUS;
	...
	MariaDB> START SLAVE;

**Note**: Already connected slaves or new ones are not affected by *STOP/START SLAVE*.
These commands only control the MaxScale connection to the master server.

### Change the Master server configuration

When router is configured and it is properly working it is possible to change the master parameters.
First step is stop the replication from the master.

	MariaDB> STOP SLAVE;

Next step is the master configuration

	MariaDB> CHANGE MASTER TO ...

A successful configuration change results in *master.ini* being updated.

Any error is reported in the MySQL and in log files

The supported options are:

	MASTER_HOST
	MASTER_PORT
	MASTER_USER
	MASTER_PASSWORD
	MASTER_LOG_FILE
	MASTER_LOG_POS
	MASTER_CONNECT_RETRY
	MASTER_HEARTBEAT_PERIOD
	MASTER_USE_GTID

and SSL options as well:

	MASTER_SSL (0|1)
	MASTER_SSL_CERT (path to certificate file)
	MASTER_SSL_KEY (path to key file)
	MASTER_SSL_CA (path to CA cerificate file)
	MASTER_TLS_VERSION (allowed level of encryption used)

Further details about level of encryption or certificates can be found here
[Configuration Guuide](../Getting-Started/Configuration-Guide.md)

There are some **constraints** related to *MASTER_LOG_FILE* and *MASTER_LOG_POS*:

*MASTER_LOG_FILE* can be changed to next binlog in sequence with *MASTER_LOG_POS=4* or
to current one at current position.

Two example **scenarios** with _file_ and _pos_:

(1) Current binlog file is ‘mysql-bin.000003', position 88888

```
MariaDB> CHANGE MASTER TO MASTER_LOG_FILE=‘mysql-bin.000003',
         MASTER_LOG_POS=8888
```

This could be applied to current master_host/port or a new one.
If there is a master server maintenance and a slave is being promoted as master it should
be checked that binlog file and position are valid: in case of any error replication stops
and errors are reported via *SHOW SLAVE STATUS* and in error logs.

(2) Current binlog file is ‘mysql-bin.000099', position 1234

```
MariaDB> CHANGE MASTER TO MASTER_LOG_FILE=‘mysql-bin.000100',
         MASTER_LOG_POS=4
```

This could be applied with current master_host/port or a new one. If transaction safety option is on
and the current binlog file contains an incomplete transaction it will be truncated to the position
where transaction started.
In such situation a proper message is reported in MySQL connection and with next START SLAVE binlog
file truncation will occur and MariaDB MaxScale will request events from the master using the next
binlog file at position 4.

The above scenario might refer to a master crash/failure:
the new server that has just been promoted as master, doesn't have last transaction events and
should have the new binlog file, the next one in sequence (some `FLUSH LOGS` commands must be issued).
Truncating the previous MariaDB MaxScale binlog is safe as that incomplete transaction is lost.
It should be checked that current master or new one has the new binlog file, in case of any error
replication stops and errors are reported via *SHOW SLAVE STATUS* and in error logs.

	MariaDB> START SLAVE;

Check for any error in log files and with

	MariaDB> SHOW SLAVE STATUS;

In some situations replication state could be *STOPPED* and proper messages are displayed in
error logs and in *SHOW SLAVE STATUS*,

##### MariaDB 10 GTID

If _mariadb10_master_gtid_ is On changing the master doesn't require the setting of a
new _file_ and _pos_, just specify new host and port with CHANGE MASTER.

As the binlog files will be automatically saved using a hierarchy model
(_binlogdir/domain_id/server_id/_filename_), MaxScale can work with any filename and any
sequence and no binlog file will be overwritten by accident.

**Scenario** example:

Let's start saying it's a good practice to issue in the new Master `FLUSH TABLES` which
causes a new GTID to be written in the master binlog file, incrementing the previous sequence:

MaxScale has last GTID from former master `0-10124-282`:

```
MariaDB> SELECT @@global.gtid_current_pos;
+---------------------------+
| @@global.gtid_current_pos |
+---------------------------+
|               0-10124-282 |
+---------------------------+
```
and binlog name and pos are:

```
MySQL [(none)]> show master status\G
*************************** 1. row ***************************
     File: mysql-bin.000060
     Position: 4434
```

The new Master, **server_id 10333**, has new GTID:

```
MariaDB> FLUSH TABLES;
MariaDB> SELECT @@global.gtid_current_pos;
+---------------------------+
| @@global.gtid_current_pos |
+---------------------------+
|               0-10333-283 |
+---------------------------+
```

Starting the replication in MaxScale, `START SLAVE`,
will result in new events being downloaded and stored in the new file
`0/10333/mysql-bin.000001` (which should be the current file in the new master)

As usual, check for any error in log files and with

	MariaDB> SHOW SLAVE STATUS;

Issuing the admin command `SHOW BINARY LOGS` it's possible to see the list
of log files which have been downloaded and to follow the _master change_
history: the displayed log file names have a prefix with
replication domain_id and server_id.

```
MariaDB> SHOW BINARY LOGS;
+--------------------------+-----------+
| Log_name                 | File_size |
+--------------------------+-----------+
| 0/10122/binlog.000063    |      1543 |
| 0/10122/binlog.000064    |       621 |
...
| 0/10116/mysql-bin.000060 |      4590 |
...
| 0/10116/mysql-bin.000112 |      9699 |
...
| 0/10124/log-bin.000016   |      2112 |
```

### Configuration reset
In order to resolve any mistake done with *CHANGE MASTER TO MASTER_LOG_FILE / MASTER_LOG_POS*,
another administrative command is helpful.

	MariaDB> RESET SLAVE;

This command removes *master.ini* file, blanks all master configuration in memory
and sets binlog router in unconfigured state: a *CHANGE MASTER TO* command should
be issued for the new configuration.

**Note**:

- Existing binlog files are not touched by this command.
- Existing GTID is set to empty value.
- Existing GTID database in binlogdir (gtid_maps.db) is not touched.

### Removing binary logs from binlogdir

Since version 2.2, if `mariadb10_slave_gtid` or `mariadb10_master_gtid`
are set to On, it's possible to remove the binlog files from _binlogdir_
and delete related entries in GTID repository using the admin
command `PURGE BINARY LOGS TO 'file'`

Example:

Remove all binlog files up to 'file.0001'.

```
MariaDB> PURGE BINARY LOGS TO 'file.0001';
```

**Note**: the current binlog file cannot be removed.

If needed, after purging all the files issue `RESET SLAVE` and manually
remove last binlog file and the GTID database (`gtid_maps.db`) in _binlog_dir_.

###SSL options:

	MariaDB> CHANGE MASTER TO MASTER_SSL = 1, MASTER_SSL_CERT='/home/maxscale/packages/certificates/client/client-cert.pem', MASTER_SSL_CA='/home/maxscale/packages/certificates/client/ca.pem', MASTER_SSL_KEY='/home/maxscale/packages/certificates/client/client-key.pem', MASTER_TLS_VERSION='TLSv12';

	MariaDB> CHANGE MASTER TO MASTER_TLS_VERSION='TLSv12';

	MariaDB> CHANGE MASTER TO MASTER_SSL = 0;


#### Some constraints:
  - In order to enable/re-enable Master SSL comunication the MASTER_SSL=1 option is required and all certificate options must be explicitey set in the same CHANGE MASTER TO command.
  - New certificate options changes take effect after maxScale restart or after MASTER_SSL=1 with the new options.

Note:
 - SHOW SLAVE STATUS displays all the options but MASTER_TLS_VERSION value.
 - Maxadmin, 'show services' or 'show service $binlog_service' displays all the options when SSL is on.
 - STOP SLAVE is required for CHANGE MASTER TO command (any option)
 - START SLAVE will use new SSL options for Master SSL communication setup.

Examples:
  mysql client

	MariaDB> SHOW SLAVE STATUS\G

           Master_SSL_Allowed: Yes
           Master_SSL_CA_File: /home/mpinto/packages/certificates/client/ca.pem
           Master_SSL_CA_Path:
              Master_SSL_Cert: /home/mpinto/packages/certificates/client/client-cert.pem
            Master_SSL_Cipher:
               Master_SSL_Key: /home/mpinto/packages/certificates/client/client-key.pem

  maxadmin client

	MaxScale>'show service BinlogServer'

	Service:                             BinlogServer
	Router:                              binlogrouter (0x7fd8c3002b40)
	State:                               Started
	Master connection DCB:               0x7fd8c8fce228
	Master SSL is ON:
		Master SSL CA cert: /home/mpinto/packages/certificates/client/ca.pem
		Master SSL Cert:    /home/mpinto/packages/certificates/client/client-cert.pem
		Master SSL Key:     /home/mpinto/packages/certificates/client/client-key.pem
		Master SSL tls_ver: TLSv12



### Slave servers setup

Examples of *CHANGE MASTER TO* command issued on a slave server that wants
to get replication events from MariaDB MaxScale binlog router:

```
CHANGE MASTER TO MASTER_HOST=‘$maxscale_IP’,
MASTER_PORT=5308,
MASTER_USER='repl',
MASTER_PASSWORD=‘somepasswd’,
MASTER_LOG_FILE=‘mysql-bin.000001'

CHANGE MASTER TO MASTER_HOST=‘$maxscale_IP’,
MASTER_PORT=5308,
MASTER_USER='repl',
MASTER_PASSWORD=‘somepasswd’,
MASTER_LOG_FILE=‘mysql-bin.000159',
MASTER_LOG_POS=245
```

The latter example specifies a *MASTER_LOG_POS* for the selected *MASTER_LOG_FILE*

Note:

 - *MASTER_LOG_FILE* must be set to one of existing binlog files in MariaDB MaxScale binlogdir

 - If *MASTER_LOG_POS* is not set with *CHANGE MASTER TO* it defaults to 4

 - Latest binlog file name and pos in MariaDB MaxScale could be found via maxadmin
output or from mysql client connected to MariaDB MaxScale:

Example:

```
-bash-4.1$ mysql -h 127.0.0.1 -P 5308 -u$user -p$pass

	MySQL [(none)]> show master status\G
	*************************** 1. row ***************************
   	         File: mysql-bin.000181
	         Position: 2569
```

##### MariaDB 10 GTID
If connecting slaves are MariaDB 10.x it's also possible to connect with GTID,
*mariadb10_slave_gtid=On* has to be set in configuration before starting MaxScale.

```
SET @@global.gtid_slave_pos='';
```
or using a know value:

```
SET @@global.gtid_slave_pos='0-10122-230';
```

**Note:** If empty GTID, MaxScale will send binlog events from the beginning of its current binlog file.

In order to get the latest GTID from MaxScale *@@global.gtid_current_pos* variable is available:

```
MariaDB> SELECT @@global.gtid_current_pos;
+---------------------------+
| @@global.gtid_current_pos |
+---------------------------+
|               0-10124-282 |
+---------------------------+
```
Complete example of MariaDB 10 Slave connection to MaxScale with GTID:

```
MariaDB> SET @@global.gtid_slave_pos='0-10122-230';
MariaDB> CHANGE MASTER TO
         MASTER_HOST='127.0.0.1',
         MASTER_PORT=10122,
         MASTER_USE_GTID=Slave_pos, ...
MariaDB> START SLAVE;
```

Additionally, if *mariadb10_slave_gtid=On*, it's also possible to retrieve the list of binlog files downloaded from the master with the new admin command _SHOW BINARY LOGS_:

```
MariaDB> SHOW BINARY LOGS;
+------------------+-----------+
| Log_name         | File_size |
+------------------+-----------+
| mysql-bin.000063 |      1543 |
| mysql-bin.000064 |       621 |
| mysql-bin.000065 |      1622 |
| mysql-bin.000066 |       582 |
| mysql-bin.000067 |       465 |
+------------------+-----------+
```

# Enabling MariaDB 10 compatibility

MariaDB 10 has different slave registration phase so an option is required:

```
router_options=...., mariadb10-compatibility=On
```

*version_string* should be modified in order to present MariaDB 10 version when
MariaDB MaxScale sends server handshake packet.

```
version_string=10.1.21-log
```

# New MariaDB events in Diagnostics

With a MariaDB 10 setup new events are displayed when master server is MariaDB 10.

```
MariaDB 10 Annotate Rows Event 0
MariaDB 10 Binlog Checkpoint Event 0
MariaDB 10 GTID Event 0
MariaDB 10 GTID List Event 0
```

# How to include a Binlog server setup with MaxScale SQL routers

Since MaxScale 2.2 it's possible to use a replication setup
which includes Binlog Server, MaxScale MySQL monitor and SQL routers
such as ReadConnection and ReadWriteSplit.

The required action is to add the binlog server to the MySQL
monitor server list only if _master_id_ identity is set.

Example for binlog server setup with master_id=2222

```
[BinlogServer]
type=service
router=binlogrouter
router_options=server-id=93,,,,master_id=2222
```

MaxScale configuration with SQL routers:

```
[binlog_server]
type=server
address=192.168.100.100
port=8808

[MySQL Monitor]
type=monitor
module=mysqlmon
servers=server5,server2,server1,binlog_server
```

Binlog Server is then seen as a Relay Master without the Slave status:

```
MaxScale> show servers
Server 0x65c920 (binlog_server)
	Server:                              192.168.100.100
	Status:                              Relay Master, Running
	Protocol:                            MySQLBackend
	Port:                                8808
	Server Version:                      10.1.17-log
	Node Id:                             2222
	Master Id:                           10124
	Slave Ids:                           1, 104
	Repl Depth:                          1

Server 0x65b690 (server5)
	Server:                              127.0.0.1
	Status:                              Master, Running
	Protocol:                            MySQLBackend
	Port:                                10124
	Server Version:                      10.1.24-MariaDB
	Node Id:                             10124
	Master Id:                           -1
	Slave Ids:                           2222
	Repl Depth:                          0
```

If Binlog Server is monitored and no explicit identity is made by using _master_id_,
then it's not shown as a Relay Master but just as Running server and its slaves
are listed in the Master details (including binlog server id):

```
Server 0x65c910 (binlog_server)
	Server:                              192.168.100.100
	Status:                              Running
	Protocol:                            MySQLBackend
	Port:                                8808
	Server Version:                      10.1.17-log
	Node Id:                             93
	Master Id:                           10124
	Slave Ids:
	Repl Depth:                          1

Server 0x65b680 (server5)
	Server:                              127.0.0.1
	Status:                              Master, Running
	Protocol:                            MySQLBackend
	Port:                                10124
	Server Version:                      10.1.24-MariaDB
	Node Id:                             10124
	Master Id:                           -1
	Slave Ids:                           1, 104 , 93
	Repl Depth:                          0
```

**Note**: even without the _master_id_ router option, it's always worth monitoring
the Binlog Server in order to show all servers included in the replication setup.
