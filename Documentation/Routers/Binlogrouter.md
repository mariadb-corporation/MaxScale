# Binlogrouter

The binlogrouter is a replication protocol proxy module for MariaDB MaxScale. This module allows MariaDB MaxScale to connect to a master server and retrieve binary logs while slave servers can connect to MariaDB MaxScale like they would connect to a normal master server. If the master server goes down, the slave servers can still connect to MariaDB MaxScale and read binary logs. You can switch to a new master server without the slaves noticing that the actual master server has changed. This allows for a more highly available replication setup where replication is high-priority.

# Configuration

## Mandatory Router Parameters

The binlogrouter requires the `server`, `user` and `passwd` parameters. These should be configured according to the [Configuration Guide](../Getting-Started/Configuration-Guide.md#service).

In addition to these two parameters, `router_options` needs to be defined. This is the main way the binlogrouter is configured and it will be covered in detail in the next section.

## Router Options

Binlogrouter is configured with a comma-separated list of key-value pairs. The following options should be given as a value to the `router_options` parameter.

### `binlogdir`

This parameter allows the location that MariaDB MaxScale uses to store binlog files to be set. If this parameter is not set to a directory name then MariaDB MaxScale will store the binlog files in the directory /var/cache/maxscale/<Service Name>.
In the binlog dir there is also the 'cache' directory that contains data retrieved from the master during registration phase and the master.ini file which contains the configuration of current configured master.

### `uuid`

This is used to set the unique uuid that the binlog router uses when it connects to the master server.
If no explicit value is given for the uuid in the configuration file then a uuid will be generated.

### `server-id`

As with uuid, MariaDB MaxScale must have a unique server-id for the connection it makes to the master, this parameter provides the value of server-id that MariaDB MaxScale will use when connecting to the master.

### `master-id`

The server-id value that MariaDB MaxScale should use to report to the slaves that connect to MariaDB MaxScale.
This may either be the same as the server-id of the real master or can be chosen to be different if the slaves need to be aware of the proxy layer.
The real master server-id will be used if the option is not set.

### `master_uuid`

It is a requirement of replication that each slave have a unique UUID value. The MariaDB MaxScale router will identify itself to the slaves using the uuid of the real master if this option is not set.

### `master_version`

The MariaDB MaxScale router will identify itself to the slaves using the server version of the real master if this option is not set.

### `master_hostname`

The MariaDB MaxScale router will identify itself to the slaves using the server hostname of the real master if this option is not set.

### `user`

This is the user name that MariaDB MaxScale uses when it connects to the master. This user name must have the rights required for replication as with any other user that a slave uses for replication purposes. If the user parameter is not given in the router options then the same user as is used to retrieve the credential information will be used for the replication connection, i.e. the user in the service entry.

This user is the only one available for MySQL connection to MaxScale Binlog Server for administration when master connection is not done yet.

The user that is used for replication, either defined using the user= option in the router options or using the username and password defined of the service must be granted replication privileges on the database server.

```
    MariaDB> CREATE USER 'repl'@'maxscalehost' IDENTIFIED by 'password';
    MariaDB> GRANT REPLICATION SLAVE ON *.* TO 'repl'@'maxscalehost';
```

### `password`

The password of the above user. If the password is not explicitly given then the password in the service entry will be used. For compatibility with other username and password definitions within the MariaDB MaxScale configuration file it is also possible to use the parameter passwd=.

### `heartbeat`

This defines the value of the heartbeat interval in seconds for the connection to the master. MariaDB MaxScale requests the master to ensure that a binlog event is sent at least every heartbeat period. If there are no real binlog events to send the master will sent a special heartbeat event. The default value for the heartbeat period is every 5 minutes. The current interval value is reported in the diagnostic output.

### `burstsize`

This parameter is used to define the maximum amount of data that will be sent to a slave by MariaDB MaxScale when that slave is lagging behind the master. In this situation the slave is said to be in "catchup mode", this parameter is designed to both prevent flooding of that slave and also to prevent threads within MariaDB MaxScale spending disproportionate amounts of time with slaves that are lagging behind the master. The burst size can be defined in Kb, Mb or Gb by adding the qualifier K, M or G to the number given. The default value of burstsize is 1Mb and will be used if burstsize is not given in the router options.

### `mariadb10-compatibility`

This parameter allows binlogrouter to replicate from a MariaDB 10.0 master server. GTID will not be used in the replication.

```
# Example
router_options=mariadb10-compatibility=1
```

### `transaction_safety`

This parameter is used to enable/disable incomplete transactions detection in binlog router.
When MariaDB MaxScale starts an error message may appear if current binlog file is corrupted or an incomplete transaction is found.
During normal operations binlog events are not distributed to the slaves until a COMMIT is seen.
The default value is off, set transaction_safety=on to enable the incomplete transactions detection.

### `send_slave_heartbeat`

This defines whether (on | off) MariaDB MaxScale sends to the slave the heartbeat packet when there are no real binlog events to send. The default value if 'off', no heartbeat event is sent to slave server. If value is 'on' the interval value (requested by the slave during registration) is reported in the diagnostic output and the packet is send after the time interval without any event to send.

### `semisync`

This parameter controls whether binlog server could ask Master server to start the Semi-Synchronous replication.
In order to get semi-sync working the Master server must have the *rpl_semi_sync_master* plugin installed.
The available plugin and the value of GLOBAL VARIABLE *rpl_semi_sync_master_enabled* are checked in the Master registration phase: if plugin is installed in the Master database the binlog server then requests the semi-sync option.
Note:
 - the network replication stream from Master has two additional bytes before each binlog event.
 - the Semi-Sync protocol requires an acknoledge packet to be sent back to Master only when requested: the semi-sync flag will have value of 1.
   This flag is set only if *rpl_semi_sync_master_enabled=1* in the Master, otherwise it will always have value of 0 and no ack packet is sent back.

Please note that semi-sync replication is only related to binlog server to Master communication.
### `ssl_cert_verification_depth`

This parameter sets the maximum length of the certificate authority chain that will be accepted. Legal values are positive integers. This applies to SSL connection to master server that could be acivated either by writing options in master.ini or later via CHANGE MASTER TO. This parameter cannot be modified at runtime, default is 9.

A complete example of a service entry for a binlog router service would be as follows.
```
    [Replication]
    type=service
    router=binlogrouter
    servers=masterdb
    version_string=5.6.17-log
    user=maxscale
    passwd=Mhu87p2D
    router_options=uuid=f12fcb7f-b97b-11e3-bc5e-0401152c4c22,server-id=3,user=repl,password=slavepass,master-id=1,heartbeat=30,binlogdir=/var/binlogs,transaction_safety=1,master_version=5.6.19-common,master_hostname=common_server,master_uuid=xxx-fff-cccc-common,master-id=999,mariadb10-compatibility=1,send_slave_heartbeat=1,ssl_cert_verification_depth=9,semisync=1
```

The minimum set of router options that must be given in the configuration are are server-id and master-id, default values may be used for all other options.

## Examples

The [Replication Proxy](../Tutorials/Replication-Proxy-Binlog-Router-Tutorial.md) tutorial will show you how to configure and administrate a binlogrouter installation.


Tutorial also includes SSL communication setup to the master server and SSL client connections setup to MaxScale Binlog Server
