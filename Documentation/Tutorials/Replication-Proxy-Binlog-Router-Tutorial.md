# MaxScale as a replication proxy
MaxScale was designed as a highly configurable proxy that sits between a database layer and the clients of that database, the binlog router described here is somewhat different to that original concept, moving MaxScale down to play a role within the database layer itself.

In a traditional MySQL replication setup a single master server is created and a set of slaves MySQL instances are configured to pull the binlog files from that master to the slaves. There are some problems however in this setup; when the number of slaves servers starts to increase an increasing load is placed on the master to serve the binlogs to each slave. When a master server fails every slave server requires some action to be performed before a new server can become the master server.

Introducing a proxy layer between the master server and the slave servers can improve the situation by reducing the load on the master to simply serving the proxy layer rather than all of the slaves and the slaves only need to be aware of the proxy layer and not the real master server. Removing this requirement for the slaves to have knowledge of the master greatly simplifies the process of replacing a failed master within a replication environment.

## MariaDB/MySQL as a replication proxy
The most obvious solution to the requirement for a proxy layer within a replication environment is to use a MariaDB or MySQL database instance. The database server is designed to allow this, since a slave server is able to be configured such that it will produce binary logs for updates it has itself received via replication from the master server. This is done with the log_slave_updates configuration option of the server. In this case the server is known as an intermediate master, it is both a slave to the real master and a master to the other slaves in the configuration.

Using an intermediate master does not however solve all the problems and introduces some due to the way replication is implemented. A slave server reads the binary log data and creates a relay log from that binary log. This then provides a source of SQL statements which are executed within the slave in order to make the same changes to the databases on the slaves as were made on the master. If the log_slave_updates option has been enabled new binary log entries are created for the statements executed from the relay log. This means that the data in the binary log of the intermediate master is not a direct copy of the data that was received from the binary log of the real master. The resultant changes to the database will be the same, provided no updates have been performed on the intermediate master that did not originate on the real master, but the steps to achieve those changes may be different. In particular if group commit functionality is used, to allow multiple transactions to commit in parallel, these may well be different on the intermediate master. This can cause a reduction in the parallelism of the commits and a subsequent reduction in the performance of the slave servers.

This re-execution of the SQL statements also adds latency to the intermediate master solution, since the full process of parsing, optimization and execution must occur for every statement that is replicated from the master to the slaves must be performed in the intermediate master. This latency introduces lag in the replication chain, with a greater delay being introduced from the time a transaction is committed on the master until the data is available on the slaves.

Use of an intermediate master does improve the process of failover of the master server, since the slaves are only aware of the intermediate master the process of promoting one of the existing slaves to become the new master only involves that slave and the intermediate master. A slave can become the new master as soon as all the changes from the intermediate master have been processed. The intermediate master then needs to be reset to the correct point in the binary log of the new master and replication can continue.

An added complexity that needs to be dealt with is the failure of the intermediate master itself. If this occurs then the same problem as described earlier exists, all slaves must be updated when a new intermediate master is created. If multiple intermediate masters are used there is also a restriction that slaves can not be moved from the failed intermediate master to another intermediate master due to the fact that the binlog on the different intermediate nodes are not guaranteed to be the same. 

## MaxScale's approach
MaxScale takes a much simpler approach to the process of being a replication proxy. It acts as a slave to the real master and as a master to the slaves in the same way as an intermediate master does, however it does not implement any re-execution of the statements within the binary log. MaxScale creates a local cache of the binary logs it receives from the master and it will serve binary log events to the slaves from this cache of the master's binary log. This means that the slaves will always get binary log events that have a one-to-one correlation to those written by the master. Parallelism in the binary log events of the master is maintained in the events that are observed by the slaves.

In the MaxScale approach the latency that is introduced is mostly the added network latency associated with adding the extra network hop. There is no appreciable processing performed at the MaxScale level, other than for managing the local cache of the binlog files.

In addition every MaxScale that is acting as a proxy of the master will have exactly the same binlog events as the master itself. This means that a slave can be moved between any of the MaxScale server or to the real master without the need to perform any special processing. The result is much simpler behavior for failure recovery and the ability to have a very simple, redundant proxy layer with slaves free to both between the proxies.

# Configuring MaxScale as a replication proxy
Using MaxScale as a replication proxy is much the same as using MaxScale as a proxy between the clients and the database servers. In this case the master server should be considered as the database backend and the slave servers as the clients of MaxScale.

## Service Configuration

As with any MaxScale configuration a good starting point is with the service definition with the MaxScale.cnf file. The service requires a name which is the section name in the ini file, a type parameter with a value of service and the name of the router plugin that should be loaded. In the case of replication proxies this router name is binlogrouter.


    [Replication]
    type=service
    router=binlogrouter

Other standard service parameters need to be given in the configuration section that are used to retrieve the set of users from the backend (master) database, also a version string can be given such that the MaxScale instance will report this version string to the slave servers that connect to MaxScale. The master server entry must also be given. In the current implementation of the router only a single server can be given.

    [Replication]
    type=service
    router=binlogrouter
    servers=masterdb
    version_string=5.6.17-log
    user=maxscale
    passwd=Mhu87p2D

The user and passwd entries in the above example are used in order for MaxScale to populate the credential information that is required to allow the slaves to connect to MaxScale. This user should be configured in exactly the same way a for any other MaxScale service, i.e. the user needs access to the mysql.user table and the mysql.db table as well as having the ability to perform a SHOW DATABASES command.

The final configuration requirement is the router specific options. The binlog router requires a set of parameters to be passed, these are passed in the router_options parameter of the service definition as a comma separated list of name value pairs.

### uuid

This is used to set the unique uuid that the router uses when it connects to the master server. It is a requirement of replication that each slave have a unique UUID value. The MaxScale router will identify itself to the slaves using the uuid of the real master and not this uuid. If no explicit value is given for the uuid in the configuration file then a uuid will be generated.

### server-id

As with uuid, MaxScale must have a unique server-id for the connection it makes to the master, this parameter provides the value of server-id that MaxScale will use when connecting to the master.

### user

This is the user name that MaxScale uses when it connects to the master. This user name must have the rights required for replication as with any other user that a slave uses for replication purposes. If the user parameter is not given in the router options then the same user as is used to retrieve the credential information will be used for the replication connection, i.e. the user in the service entry.

The user that is used for replication, either defined using the user= option in the router options or using the username and password defined of the service must be granted replication privileges on the database server.

    MariaDB> CREATE USER 'repl'@'maxscalehost' IDENTIFIED by 'password';
    MariaDB> GRANT REPLICATION SLAVE ON *.* TO 'repl'@'maxscalehost';

### password

The password of the above user. If the password is not explicitly given then the password in the service entry will be used. For compatibility with other username and password definitions within the MaxScale configuration file it is also possible to use the parameter passwd=.

### master-id

The server-id value that MaxScale should use to report to the slaves that connect to MaxScale. This may either be the same as the server-id of the real master or can be chosen to be different if the slaves need to be aware of the proxy layer.

### filestem

This parameter is used to provide the stem of the file names that are used to store the binlog events. If this parameter is not given then the events are stored in the default name of mysql-bin followed by a sequence number.

### initialfile

This optional parameter allows for the administrator to define the number of the first binlog file to download. If MaxScale has previously received binlogs it will use those existing binlog files to determine what to request from the master. If no files have been downloaded MaxScale will then ask for the binlog file with the index number defined in the initialfile parameter. If this parameter is not set then MaxScale will ask the master for binlog events from file 1.

### binlogdir

This parameter allows the location that MaxScale uses to store binlog files to be set. If this parameter is not set to a directory name then MaxScale will store the binlog files in the directory /var/cache/maxscale/<Service Name>.

### heartbeat

This defines the value of the heartbeat interval in seconds for the connection to the master. MaxScale requests the master to ensure that a binlog event is sent at least every heartbeat period. If there are no real binlog events to send the master will sent a special heartbeat event. The default value for the heartbeat period is every 5 minutes.

### burstsize

This parameter is used to define the maximum amount of data that will be sent to a slave by MaxScale when that slave is lagging behind the master. In this situation the slave is said to be in "catchup mode", this parameter is designed to both prevent flooding of that slave and also to prevent threads within MaxScale spending disproportionate amounts of time with slaves that are lagging behind the master. The burst size can be defined in Kb, Mb or Gb by adding the qualifier K, M or G to the number given. The default value of burstsize is 1Mb and will be used if burstsize is not given in the router options.

A complete example of a service entry for a binlog router service would be as follows.

    [Replication]
    type=service
    router=binlogrouter
    servers=masterdb
    version_string=5.6.17-log
    router_options=uuid=f12fcb7f-b97b-11e3-bc5e-0401152c4c22,server-id=3,user=repl,password=slavepass,master-id=1,filestem=mybin,heartbeat=30,binlogdir=/home/mriddoch/binlogs
    user=maxscale
    passwd=Mhu87p2D

The minimum set of router options that must be given in the configuration are are server-id and aster-id, default values may be used for all other options.

## Listener Section

As per any service in MaxScale a listener section is required to define the address, port and protocol that is used to listen for incoming connections. In this case those incoming connections will originate from the slave servers.

    [Replication Listener]
    type=listener
    service=Replication
    protocol=MySQLClient
    port=5308

The protocol used by slaves for connection to MaxScale is the same MySQLClient protocol that is  used for client applications to connect to databases, therefore the same MaxScale protocol module can be used.

## Master Server Section

The master server is defined in a section within the MaxScale configuration file in the same way as any other server. The protocol that is used is the same backend protocol as is used in other configurations.

    [masterdb]
    type=server
    address=178.62.50.70
    port=3306
    protocol=MySQLBackend

#  MaxScale replication diagnostics

The binlog router module of MaxScale produces diagnostic output that can be viewed via the `maxadmin` client application. Running the maxadmin command and issuing a show service command will produce a considerable amount of output that will show both the master connection status and statistics and also a block for each of the slaves currently connected.

    -bash-4.1$ maxadmin show service Replication
    Service 0x1567ef0
    	Service:				Replication
    	Router: 				binlogrouter (0x7f4ceb96a820)
    	State: 					Started
    	Master connection DCB:  					0x15693c0
    	Master connection state:					Binlog Dump
    	Binlog directory:						    /home/mriddoch/binlogs
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
    	Last event from master at:  			Thu Jan 29 16:41:53 2015
    						(1 seconds ago)
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
    -bash-4.1$






