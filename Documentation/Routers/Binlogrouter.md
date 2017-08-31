# Binlogrouter

The binlogrouter is a replication protocol proxy module for MariaDB
MaxScale. This module allows MariaDB MaxScale to connect to a master server and
retrieve binary logs while slave servers can connect to MariaDB MaxScale like
they would connect to a normal master server. If the master server goes down,
the slave servers can still connect to MariaDB MaxScale and read binary
logs. You can switch to a new master server without the slaves noticing that the
actual master server has changed. This allows for a more highly available
replication setup where replication is high-priority.

# Configuration

## Mandatory Router Parameters

The binlogrouter requires the `server`, `user` and `password` parameters. These
should be configured according to the
[Configuration Guide](../Getting-Started/Configuration-Guide.md#service).

In addition to these two parameters, `router_options` needs to be defined. This
is the main way the binlogrouter is configured and it will be covered in detail
in the next section.

**Note:** As of version 2.1 of MaxScale, all of the router options can also be
defined as parameters. The values defined in _router_options_ will have priority
over the parameters.

## Router Options

Binlogrouter is configured with a comma-separated list of key-value pairs. The
following options should be given as a value to the `router_options` parameter.

### `binlogdir`

This parameter controls the location where MariaDB MaxScale stores the binary log
files. If this parameter is not set to a directory name then MariaDB
MaxScale will store the binlog files in the directory
`/var/cache/maxscale/<Service Name>` where `<Service Name>` is the name of the
service in the configuration file.  The _binlogdir_ also contains the
_cache_ subdirectory which stores data retrieved from the master during the slave
registration phase. The master.ini file also resides in the _binlogdir_. This
file keeps track of the current master configuration and it is updated when a
`CHANGE MASTER TO` query is executed.

From 2.1 onwards, the 'cache' directory is stored in the same location as other
user credential caches. This means that with the default options, the user
credential cache is stored in
`/var/cache/maxscale/<Service Name>/<Listener Name>/cache/`.

Read the [MySQL Authenticator](../Authenticators/MySQL-Authenticator.md)
documentation for instructions on how to define a custom location for the user
cache.

### `uuid`

This is used to set the unique UUID that the binlog router uses when it connects
to the master server. If no explicit value is given for the UUID in the
configuration file then a UUID will be generated.

### `server_id`

As with UUID, MariaDB MaxScale must have a unique _server_id_. This parameter
configures the value of the _server_id_ that
MariaDB MaxScale will use when connecting to the master.

Older versions of MaxScale allowed the ID to be specified using `server-id`.
This has been deprecated and will be removed in a future release of MariaDB MaxScale.

### `master_id`

The _server_id_ value that MariaDB MaxScale should use to report to the slaves
that connect to MariaDB MaxScale. This may either be the same as the server id
of the real master or can be chosen to be different if the slaves need to be
aware of the proxy layer. The real master server ID will be used if the option
is not set.

Older versions of MaxScale allowed the ID to be specified using `master-id`.
This has been deprecated and will be removed in a future release of MariaDB MaxScale.

### `master_uuid`

It is a requirement of replication that each slave has a unique UUID value. The
MariaDB MaxScale router will identify itself to the slaves using the UUID of the
real master if this option is not set.

### `master_version`

By default, the router will identify itself to the slaves using the server
version of the real master. This option allows the router to use a custom version string.

### `master_hostname`

By default, the router will identify itself to the slaves using the
hostname of the real master. This option allows the router to use a custom hostname.

### `slave_hostname`

Since MaxScale 2.1.6 the router can optionally identify itself
to the master using a custom hostname.
The specified hostname can be seen in the master via
`SHOW SLAVE HOSTS` command.
The default is not to send any hostname string during registration.

### `user`

This is the user name that MariaDB MaxScale uses when it connects to the
master. This user name must have the rights required for replication as with any
other user that a slave uses for replication purposes. If the user parameter is
not given in the router options then the same user as is used to retrieve the
credential information will be used for the replication connection, i.e. the
user in the service entry.

This user is the only one available for MySQL connection to MaxScale Binlog
Server for administration when master connection is not done yet.

In MaxScale 2.1, the service user injection is done by the MySQLAuth
authenticator module. Read the
[MySQL Authenticator](../Authenticators/MySQL-Authenticator.md)
documentation for more details.

The user that is used for replication, either defined using the user= option in
the router options or using the username and password defined of the service
must be granted replication privileges on the database server.

```
CREATE USER 'repl'@'maxscalehost' IDENTIFIED by 'password';
GRANT REPLICATION SLAVE ON *.* TO 'repl'@'maxscalehost';
```

### `password`

The password for the user. If the password is not explicitly given then the
password in the service entry will be used. For compatibility with other
username and password definitions within the MariaDB MaxScale configuration file
it is also possible to use the parameter passwd=.

### `heartbeat`

This defines the value of the heartbeat interval in seconds for the connection
to the master. MariaDB MaxScale requests the master to ensure that a binlog
event is sent at least every heartbeat period. If there are no real binlog
events to send the master will sent a special heartbeat event. The default value
for the heartbeat period is every 5 minutes. The current interval value is
reported in the diagnostic output.

### `burstsize`

This parameter is used to define the maximum amount of data that will be sent to
a slave by MariaDB MaxScale when that slave is lagging behind the master. In
this situation the slave is said to be in "catchup mode", this parameter is
designed to both prevent flooding of that slave and also to prevent threads
within MariaDB MaxScale spending disproportionate amounts of time with slaves
that are lagging behind the master. The burst size can be provided as specified
[here](../Getting-Started/Configuration-Guide.md#sizes), except that IEC
binary prefixes can be used as suffixes only from MaxScale 2.1 onwards.
The default value is `1M`, which will be used if `burstsize` is not provided in
the router options.

### `mariadb10-compatibility`

This parameter allows binlogrouter to replicate from a MariaDB 10.0 master
server. GTID will not be used in the replication.

```
# Example
router_options=mariadb10-compatibility=1
```

### `transaction_safety`

This parameter is used to enable/disable incomplete transactions detection in
binlog router. When MariaDB MaxScale starts an error message may appear if
current binlog file is corrupted or an incomplete transaction is found. During
normal operations binlog events are not distributed to the slaves until a COMMIT
is seen. The default value is off, set transaction_safety=on to enable the
incomplete transactions detection.

### `send_slave_heartbeat`

This defines whether MariaDB MaxScale sends the heartbeat packet to the slave
when there are no real binlog events to send. The default value
is 'off' and no heartbeat events are sent to slave servers. If value is 'on' the
interval value (requested by the slave during registration) is reported in the
diagnostic output and the packet is send after the time interval without any
event to send.

### `semisync`

This parameter controls whether binlog server could ask Master server to start
the Semi-Synchronous replication. In order to get semi-sync working, the Master
server must have the *rpl_semi_sync_master* plugin installed. The availability
of the plugin and the value of the GLOBAL VARIABLE
*rpl_semi_sync_master_enabled* are checked in the Master registration phase: if
the plugin is installed in the Master database, the binlog server subsequently
requests the semi-sync option.

Note:
 - the network replication stream from Master has two additional bytes before
   each binlog event.
 - the Semi-Sync protocol requires an acknowledge packet to be sent back to
   Master only when requested: the semi-sync flag will have value of 1.
   This flag is set only if *rpl_semi_sync_master_enabled=1* is set in the
   Master, otherwise it will always have value of 0 and no ack packet is sent
   back.

Please note that semi-sync replication is only related to binlog server to
Master communication.

### `ssl_cert_verification_depth`

This parameter sets the maximum length of the certificate authority chain that
will be accepted. Legal values are positive integers. This applies to SSL
connection to master server that could be acivated either by writing options in
master.ini or later via CHANGE MASTER TO. This parameter cannot be modified at
runtime, default is 9.

### `encrypt_binlog`

Whether to encrypt binlog files: the default is Off.

When set to On the binlog files will be encrypted using specified AES algorithm
and the KEY in the specified key file.

**Note:** binlog encryption must be used while replicating from a MariaDB 10.1
server and serving data to MariaDB 10.x slaves. In order to use binlog
encryption the master server MariaDB 10.1 must have encryption active
(encrypt-binlog=1 in my.cnf). This is required because both master and maxscale
must store encrypted data for a working scenario for Secure
data-at-rest. Additionally, as long as Master server doesn't send the
StartEncryption event (which contains encryption setup information for the
binlog file), there is a position gap between end of FormatDescription event pos
and next event start pos. The StartEncryption event size is 36 or 40 (depending
on CRC32 being used), so the gap has that size.

MaxScale binlog server adds its own StartEncryption to binlog files consequently
the binlog events positions in binlog file are the same as in the master binlog
file and there is no position mismatch.

### `encryption_algorithm`

The encryption algorithm, either 'aes_ctr' or 'aes_cbc'. The default is 'aes_cbc'

### `encryption_key_file`

The specified key file must contains lines with following format:

`id;HEX(KEY)`

Id is the scheme identifier, which must have the value 1 for binlog encryption
, the ';' is a separator and HEX(KEY) contains the hex representation of the KEY.
The KEY must have exact 16, 24 or 32 bytes size and the selected algorithm
(aes_ctr or aes_cbc) with 128, 192 or 256 ciphers will be used.

**Note:** the key file has the same format as MariaDB 10.1 server so it's
possible to use an existing key file (not ecncrypted) which could contain
several scheme;keys: only key id with value 1 will be parsed, and if not found
an error will be reported.

Example:

```
#
# This is the Encryption Key File
# key id 1 is for binlog files encryption: it's mandatory
# The keys come from a 32bytes value, 64 bytes with HEX format
#
2;abcdef1234567890abcdef12345678901234567890abcdefabcdef1234567890
1;5132bbabcde33ffffff12345ffffaaabbbbbbaacccddeee11299000111992aaa
3;bbbbbbbbbaaaaaaabbbbbccccceeeddddd3333333ddddaaaaffffffeeeeecccd
```


### `mariadb10_slave_gtid`
If enabled this option allows MariaDB 10.x slave servers to connect to binlog
server using GTID value instead of binlog_file name and position.
MaxScale saves all the incoming MariaDB GTIDs (DDLs and DMLs)
in a sqlite3 database located in _binlogdir_ (`gtid_maps.db`).
When a slave server connects with a GTID request a lookup is made for
the value match and following binlog events will be sent.
Default option value is _off_.

Example of a MariaDB 10.x slave connection to MaxScale

```
MariaDB> SET @@global.gtid_slave_pos='0-10122-230';
MariaDB> CHANGE MASTER TO
         MASTER_HOST='192.168.10.8',
         MASTER_PORT=5306,
         MASTER_USE_GTID=Slave_pos;
MariaDB> START SLAVE;
```

**Note:** Slave servers can connect either with _file_ and _pos_ or GTID.

### `mariadb10_master_gtid`
This option allows MaxScale binlog router to register
with MariaDB 10.X master using GTID instead of _binlog_file_ name
and _position_ in CHANGE MASTER TO admin command.

The user can set a known GTID or an empty value
(in this case the Master server will send events
from it's first available binlog file).

Example of MaxScale connection to a MariaDB 10.X Master

```
# mysql -h $MAXSCALE_HOST -P $MAXCALE_PORT
MariaDB> SET @@global.gtid_slave_pos='0-198-123';
MariaDB> CHANGE MASTER TO
         MASTER_HOST='192.168.10.5',
         MASTER_PORT=3306,
         MASTER_USE_GTID=Slave_pos;
MariaDB> START SLAVE;
```

If using GTID request then it's no longer possible to use
MASTER_LOG_FILE and MASTER_LOG_POS in `CHANGE MASTER TO`
command: an error will be reported.

The default option value is _Off_, setting it to _On_
automatically sets _mariadb10_slave_gtid_ to _On_
(which enables GTID storage and GTID slave connections)

**Note:**

- When the option is _On_, **the connecting slaves can only use GTID request**:
specifying _file_ and _pos_ will end up in an error sent by MaxScale and
replication cannot start.
- The GTID request could cause the writing of events
in any position of the binlog file, whose name has been sent
by the master server before any event.
In order to avoid holes in the binlog files, MaxScale will fill all gaps
in the binlog files with ignorable events.
- It's not possible to specify the GTID _domain_id: the master one
is being used for all operations. All slave servers must use the same replication domain as the master server.

### `binlog_structure`

This option controls the way binlog file are saved in the _binlogdir_:
there are two possible values, `flat | tree`

The `tree` mode can only be set with `mariadb10_master_gtid=On`

- `flat` is the default value, files are saved as usual.
- `tree` enables the saving of files using this hierarchy model:
_binlogdir_/_domain_id_/_server_id_/_filename_

The _tree_ structure easily allows the changing of the master server
without caring about binlog filename and sequence:
just change _host_ and _port_, the replication will
resume from last GTID MaxScale has seen.

### `master_retry_count`

This option sets the maximum number of connection retries when the master server is disconnected or not reachable.
Default value is 1000.

### `connect_retry`
The option sets the time interval for a new connection retry to master server, default value is 60 seconds.


**A complete example** of a service entry for a binlog router service would be as
follows.

```
    [Replication]
    type=service
    router=binlogrouter
    servers=masterdb
    version_string=5.6.17-log
    user=maxscale
    passwd=Mhu87p2D
    router_options=uuid=f12fcb7f-b97b-11e3-bc5e-0401152c4c22,
                   server_id=3,
                   user=repl,
                   password=slavepass,
                   master_id=32,
                   heartbeat=30,
                   binlogdir=/var/binlogs,
                   transaction_safety=1,
                   master_version=5.6.19-common,
                   master_hostname=common_server,
                   master_uuid=xxx-fff-cccc-common,
                   mariadb10-compatibility=1,
                   send_slave_heartbeat=1,
                   ssl_cert_verification_depth=9,
                   semisync=1,
                   encrypt_binlog=1,
                   encryption_algorithm=aes_ctr,
                   encryption_key_file=/var/binlogs/enc_key.txt,
                   mariadb10_slave_gtid=On,
                   mariadb10_master_gtid=Off,
                   binlog_structure=flat,
                   slave_hostname=maxscale-blr-1,
                   master_retry_count=1000,
                   connect_retry=60
```

The minimum set of router options that must be given in the configuration are
`server_id` and `master_id` (unless the real master id should be used); default
values may be used for all other options.


## Examples

The [Replication Proxy](../Tutorials/Replication-Proxy-Binlog-Router-Tutorial.md) tutorial will
show you how to configure and administrate a binlogrouter installation.

Tutorial also includes SSL communication setup to the master server and SSL
client connections setup to MaxScale Binlog Server.
