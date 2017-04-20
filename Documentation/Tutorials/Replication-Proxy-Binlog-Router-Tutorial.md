# MariaDB MaxScale as a Binlog Server
MariaDB MaxScale is a dynamic data routing platform that sits between a database layer and the clients of that database. However, the binlog router described here is somewhat different to that original concept, moving MariaDB MaxScale down to play a role within the database layer itself.

In a traditional MySQL replication setup a single master server is created and a set of slaves MySQL instances are configured to pull the binlog files from that master to the slaves. There are some problems, however, in this setup; when the number of slaves grows, an increasing load caused by the serving of binlogs to each slave, is placed on the master. When the master server fails, some action must be performed on every slave server before a new server can become the master server.

Introducing a proxy layer between the master server and the slave servers can improve the situation, by reducing the load on the master to simply serving the proxy layer rather than all of the slaves. The slaves only need to be aware of the proxy layer and not of the real master server. Removing the need for the slaves to have knowledge of the actual master, greatly simplifies the process of replacing a failed master within a replication environment.

## MariaDB/MySQL as a Binlog Server
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

```
[Replication]
type=service
router=binlogrouter
```

Other standard service parameters need to be given in the configuration section that are used to retrieve the set of users from the backend (master) database, also a version string can be given such that the MariaDB MaxScale instance will report this version string to the slave servers that connect to MariaDB MaxScale.

```
[Replication]
type=service
router=binlogrouter
version_string=5.6.17-log
user=maxscale
passwd=Mhu87p2D
```

The *user* and *passwd* entries in the above example are used in order for MariaDB MaxScale to populate the credential information that is required to allow the slaves to connect to MariaDB MaxScale. This user should be configured in exactly the same way a for any other MariaDB MaxScale service, i.e. the user needs access to the *mysql.user* table and the *mysql.db* table as well as having the ability to perform a *SHOW DATABASES* command.

This user is the only one available for MySQL connection to MaxScale Binlog Server for administration when master connection is not done yet.

The master server details are currently provided by a **master.ini** file located in binlog directory and could be changed via *CHANGE MASTER TO* command issued via MySQL connection to MariaDB MaxScale; refer to the Master setup section below for further details.

In the current implementation of the router only a single server can be used.

The final configuration requirement is the router specific options. The binlog router requires a set of parameters to be passed, these are passed, as a comma separated list of name value pairs, in the *router_options* parameter of the service definition..

### binlogdir

This parameter allows the location that MariaDB MaxScale uses to store binlog files to be set. If this parameter is not set to a directory name then MariaDB MaxScale will store the binlog files in the directory */var/cache/maxscale/<Service Name>*.
In the binlog dir there is also the 'cache' directory that contains data retrieved from the master during registration phase and the *master.ini* file which contains the configuration of current configured master.

### uuid

This is used to set the unique uuid that the binlog router uses when it connects to the master server.
If no explicit value is given for the uuid in the configuration file then a uuid will be generated.

### server-id

As with uuid, MariaDB MaxScale must have a unique server-id for the connection it makes to the master, this parameter provides the value of server-id that MariaDB MaxScale will use when connecting to the master.

### master-id

The server-id value that MariaDB MaxScale should use to report to the slaves that connect to MariaDB MaxScale.
This may either be the same as the server-id of the real master or can be chosen to be different if the slaves need to be aware of the proxy layer.
The real master server-id will be used if the option is not set.

### master_uuid

It is a requirement of replication that each slave have a unique UUID value. The MariaDB MaxScale router will identify itself to the slaves using the uuid of the real master if this option is not set.

### master_version

The MariaDB MaxScale router will identify itself to the slaves using the server version of the real master if this option is not set.

### master_hostname

The MariaDB MaxScale router will identify itself to the slaves using the server hostname of the real master if this option is not set.

### user

This is the user name that MariaDB MaxScale uses when it connects to the master. This user name must have the rights required for replication as with any other user that a slave uses for replication purposes. If the user parameter is not given in the router options then the same user as is used to retrieve the credential information will be used for the replication connection, i.e. the user in the service entry.

This user is also the only one available for Binlog Server administration when the connection with master is not ready yet: the 'master.ini' file doesn't exists and no other users are available for authentication.

The user that is used for replication, either defined using the *user=* option in the router options or using the username and password defined of the service must be granted replication privileges on the database server.

```
    MariaDB> CREATE USER 'repl'@'maxscalehost' IDENTIFIED by 'password';
    MariaDB> GRANT REPLICATION SLAVE ON *.* TO 'repl'@'maxscalehost';
```

### password

The password of the above user. If the password is not explicitly given then the password in the service entry will be used. For compatibility with other username and password definitions within the MariaDB MaxScale configuration file it is also possible to use the parameter *passwd=*.

### heartbeat

This defines the value of the heartbeat interval in seconds for the connection to the master. MariaDB MaxScale requests the master to ensure that a binlog event is sent at least every heartbeat period. If there are no real binlog events to send the master will sent a special heartbeat event. The default value for the heartbeat period is every 5 minutes. The current interval value is reported in the diagnostic output.

### send_slave_heartbeat

This defines whether (on | off) MariaDB MaxScale sends to the slave the heartbeat packet when there are no real binlog events to send. The default value if 'off', no heartbeat event is sent to slave server. If value is 'on' the interval value (requested by the slave during registration) is reported in the diagnostic output and the packet is send after the time interval without any event to send.

### burstsize

This parameter is used to define the maximum amount of data that will be sent to a slave by MariaDB MaxScale when that slave is lagging behind the master. In this situation the slave is said to be in "catchup mode", this parameter is designed to both prevent flooding of that slave and also to prevent threads within MariaDB MaxScale spending disproportionate amounts of time with slaves that are lagging behind the master. The burst size can be defined in Kb, Mb or Gb by adding the qualifier K, M or G to the number given. The default value of burstsize is 1Mb and will be used if burstsize is not given in the router options.

### transaction_safety

This parameter is used to enable/disable incomplete transactions detection in binlog router.
When MariaDB MaxScale starts an error message may appear if current binlog file is corrupted or an incomplete transaction is found.
During normal operations binlog events are not distributed to the slaves until a *COMMIT* is seen.
The default value is off, set *transaction_safety=on* to enable the incomplete transactions detection.

### semisync

This parameter controls whether binlog server could ask Master server to start the Semi-Synchronous replication.
In order to get semi-sync working the Master server must have the *rpl_semi_sync_master* plugin installed.
The availability of the plugin and the value of the GLOBAL VARIABLE *rpl_semi_sync_master_enabled* are checked in the Master registration phase: if the plugin is installed in the Master database the binlog server subsequently requests the semi-sync option.

Note:
 - the network replication stream from Master has two additional bytes before each binlog event.
 - the Semi-Sync protocol requires an acknowledge packet to be sent back to Master only when requested: the semi-sync flag will have value of 1.
   This flag is set only if *rpl_semi_sync_master_enabled=1* in the Master, otherwise it will always have value of 0 and no ack packet is sent back.

Please note that semi-sync replication is only related to binlog server to Master communication.


### ssl_cert_verification_depth

This parameter sets the maximum length of the certificate authority chain that will be accepted. Legal values are positive integers.
This applies to SSL connection to master server that could be acivated either by writing options in master.ini or later via CHANGE MASTER TO.
This parameter cannot be modified at runtime, default is 9.

### `encrypt_binlog`
Whether to encrypt binlog files: the default is Off

When set to On the binlog files will be encrypted using specified AES algorithm and the KEY in the specified key file.

### `encryption_algorithm`
aes_ctr or aes_cbc

The default is 'aes_cbc'

### `encryption_key_file`
The specified key file must have this format:
a line with `1;HEX(KEY)`

Additional informatons about Binlog files encryption can be found here:
[Binlogrouter - The replication protocol proxy module for MariaDB MaxScale](../Routers/Binlogrouter.md).

A complete example of a service entry for a binlog router service would be as follows.

```
    [Replication]
    type=service
    router=binlogrouter
    version_string=5.6.17-log
    user=maxscale
    passwd=Mhu87p2D
    router_options=uuid=f12fcb7f-b97b-11e3-bc5e-0401152c4c22,server-id=3,user=repl,password=slavepass,master-id=1,heartbeat=30,binlogdir=/var/binlogs,transaction_safety=1,master_version=5.6.19-common,master_hostname=common_server,master_uuid=xxx-fff-cccc-common,master-id=999,mariadb10-compatibility=1,ssl_cert_verification_depth=9,semisync=1,encrypt_binlog=1,encryption_algorithm=aes_ctr,encryption_key_file=/var/binlogs/enc_key.txt
```

The minimum set of router options that must be given in the configuration are are *server-id* and *master-id*, default values may be used for all other options.

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

# Binlog router compatibility

Binlog Router Plugin is compatible with MySQL 5.6 and MariaDB 5.5, the current default.

In order to use it with MySQL 5.6, the *GTID_MODE* setting must be OFF and connecting slaves must not use *MASTER_AUTO_POSITION = 1* option.

It’s also works with a MariaDB 10.0 setup (master and slaves) but slave connection must not include any GTID feature.

Binlog Router currently does not work for MySQL 5.5 due to missing *@@global.binlog_checksum* variable.

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

Enabling replication from a master server requires:

        CHANGE MASTER TO MASTER_HOST=‘$master_server’, MASTER_PORT=$master_port, MASTER_USER='repl', MASTER_PASSWORD=‘somepasswd’, MASTER_LOG_FILE=‘repl-bin.000159', MASTER_LOG_POS=4

It's possible to specify the desired *MASTER_LOG_FILE* but position must be 4

The initfile option is no longer available, filestem option too it's no longer available as the stem is automatically set by parsing *MASTER_LOG_FILE*.

### Stop/start the replication

When router is configured and it is properly working it is possible to stop/start replication:

	MariaDB> STOP SLAVE;
	...
	MariaDB> SHOW SLAVE STATUS;
	...
	MariaDB> START SLAVE;

Connected or new slave connections are not affected: this *STOP/START* only controls the the connection to the master and the binlog events receiving.

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

	and SSL options as well:
	MASTER_SSL (0|1)
	MASTER_SSL_CERT (path to certificate file)
	MASTER_SSL_KEY (path to key file)
	MASTER_SSL_CA (path to CA cerificate file)
	MASTER_TLS_VERSION (allowed level of encryption used)

Further details about level of encryption or certificates could be found here [Configuration Guuide](../Getting-Started/Configuration-Guide.md)

There are some constraints related to *MASTER_LOG_FILE* and *MASTER_LOG_POS*:

*MASTER_LOG_FILE* could be changed to next binlog in sequence with *MASTER_LOG_POS=4* or to current one at current position.

Examples:

1) Current binlog file is ‘mysql-bin.000003', position 88888

	MariaDB> CHANGE MASTER TO MASTER_LOG_FILE=‘mysql-bin.000003',MASTER_LOG_POS=8888

This could be applied to current master_host/port or a new one.
If there is a master server maintenance and a slave is being promoted as master it should be checked that binlog file and position are valid: in case of any error replication stops and errors are reported via *SHOW SLAVE STATUS* and in error logs.

2) Current binlog file is ‘mysql-bin.000099', position 1234

	MariaDB> CHANGE MASTER TO MASTER_LOG_FILE=‘mysql-bin.000100',MASTER_LOG_POS=4

This could be applied with current master_host/port or a new one
If transaction safety option is on and the current binlog file contains an incomplete transaction it will be truncated to the position where transaction started.
In such situation a proper message is reported in MySQL connection and with next START SLAVE binlog file truncation will occur and MariaDB MaxScale will request events from the master using the next binlog file at position 4.

The above scenario might refer to a master crash/failure:
 the new server that has just been promoted as master doesn't have last transaction events but it should have the new binlog file (the next in sequence).
Truncating the previous MariaDB MaxScale binlog is safe as that incomplete transaction is lost.
It should be checked that current master or new one has the new binlog file, in case of any error replication stops and errors are reported via *SHOW SLAVE STATUS* and in error logs.

	MariaDB> START SLAVE;

Check for any error in log files and with

	MariaDB> SHOW SLAVE STATUS;

In some situations replication state could be *STOPPED* and proper messages are displayed in error logs and in *SHOW SLAVE STATUS*,

In order to resolve any mistake done with *CHANGE MASTER TO MASTER_LOG_FILE / MASTER_LOG_POS*, another administrative command would be helpful.

	MariaDB> RESET SLAVE;

This command removes *master.ini* file, blanks all master configuration in memory and sets binlog router in unconfigured state: a *CHANGE MASTER TO* command should be issued for the new configuration.

Note: existing binlog files are not touched by this command.

Examples with SSL options:

	MySQL [(none)]> CHANGE MASTER TO MASTER_SSL = 1, MASTER_SSL_CERT='/home/maxscale/packages/certificates/client/client-cert.pem', MASTER_SSL_CA='/home/maxscale/packages/certificates/client/ca.pem', MASTER_SSL_KEY='/home/maxscale/packages/certificates/client/client-key.pem', MASTER_TLS_VERSION='TLSv12';

	MySQL [(none)]> CHANGE MASTER TO MASTER_TLS_VERSION='TLSv12';

	MySQL [(none)]> CHANGE MASTER TO MASTER_SSL = 0;


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

	MySQL> SHOW SLAVE STATUS\G

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

Examples of *CHANGE MASTER TO* command issued on a slave server that wants to gets replication events from MariaDB MaxScale binlog router:
```
CHANGE MASTER TO MASTER_HOST=‘$maxscale_IP’, MASTER_PORT=5308, MASTER_USER='repl', MASTER_PASSWORD=‘somepasswd’,
MASTER_LOG_FILE=‘mysql-bin.000001'

CHANGE MASTER TO MASTER_HOST=‘$maxscale_IP’, MASTER_PORT=5308, MASTER_USER='repl', MASTER_PASSWORD=‘somepasswd’,
MASTER_LOG_FILE=‘mysql-bin.000159', MASTER_LOG_POS=245
```
The latter example specifies a *MASTER_LOG_POS* for the selected *MASTER_LOG_FILE*

Note:

 - *MASTER_LOG_FILE* must be set to one of existing binlog files in MariaDB MaxScale binlogdir

 - If *MASTER_LOG_POS* is not set with *CHANGE MASTER TO* it defaults to 4

 - Latest binlog file name and pos in MariaDB MaxScale could be find via maxadmin output or from mysql client connected to MariaDB MaxScale:

Example:
```
-bash-4.1$ mysql -h 127.0.0.1 -P 5308 -u$user -p$pass

	MySQL [(none)]> show master status\G
	*************************** 1. row ***************************
   	         File: mysql-bin.000181
	         Position: 2569
```
# Enabling MariaDB 10 compatibility

MariaDB 10 has different slave registration phase so an option is required:

```
router_options=...., mariadb10-compatibility=1
```

version_string should be modified in order to present MariaDB 10 version when MariaDB MaxScale sends server handshake packet.

```
version_string=10.0.17-log
```

# New MariaDB events in Diagnostics

With a MariaDB 10 setups new events are displayed when master server is MariaDB 10.

```
MariaDB 10 Annotate Rows Event 0
MariaDB 10 Binlog Checkpoint Event 0
MariaDB 10 GTID Event 0
MariaDB 10 GTID List Event 0
```
