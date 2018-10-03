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

The binlogrouter requires the `user` and `password` parameters. These should be
configured according to the
[Configuration Guide](../Getting-Started/Configuration-Guide.md#service).

In addition to these two parameters, the `server_id` and `binlogdir` parameters needs to be defined.

## Router Parameters

The binlogrouter accepts the following parameters.

**Note:** Earlier versions of MaxScale supported the configuration of the
binlogrouter only via `router_options` (a the comma-separated list of key-value
pairs). As of MaxScale 2.1, all of the router options should be defined as
parameters. The values defined in `router_options` will have priority over the
parameters to support legacy configurations. The use of `router_options` is
deprecated.

### `binlogdir`

This parameter controls the location where MariaDB MaxScale stores the binary log
files. This is a mandatory parameter.

The _binlogdir_ also contains the _cache_ subdirectory which stores data
retrieved from the master during the slave registration phase. The
master.ini file also resides in the _binlogdir_. This file keeps track of
the current master configuration and it is updated when a `CHANGE MASTER
TO` query is executed.

From 2.1 onwards, the 'cache' directory is stored in the same location as other
user credential caches. This means that with the default options, the user
credential cache is stored in
`/var/cache/maxscale/<Service Name>/<Listener Name>/cache/`.

Read the [MySQL Authenticator](../Authenticators/MySQL-Authenticator.md)
documentation for instructions on how to define a custom location for the user
cache.

### `server_id`

MariaDB MaxScale must have a unique _server_id_. This parameter configures
the value of the _server_id_ that MariaDB MaxScale will use when
connecting to the master. This is a mandatory parameter.

Older versions of MaxScale allowed the ID to be specified using `server-id`.
This has been deprecated and will be removed in a future release of MariaDB MaxScale.

### `master_id`

The _server_id_ value that MariaDB MaxScale should use to report to the slaves
that connect to MariaDB MaxScale.

This may either be the same as the server id of the real master or can be
chosen to be different if the slaves need to be aware of the proxy
layer. The real master server ID will be used if the option is not set.

Older versions of MaxScale allowed the ID to be specified using `master-id`.
This has been deprecated and will be removed in a future release of MariaDB MaxScale.

### `uuid`

This is used to set the unique UUID that the binlog router uses when it connects
to the master server. By default the UUID will be generated.

### `master_uuid`

It is a requirement of replication that each server has a unique UUID value. If
this option is not set, binlogrouter will identify itself to the slaves using
the UUID of the real master.

### `master_version`

By default, the router will identify itself to the slaves using the server
version of the real master. This option allows the router to use a custom
version string.

### `master_hostname`

By default, the router will identify itself to the slaves using the hostname of
the real master. This option allows the router to use a custom hostname.

### `slave_hostname`

Since MaxScale 2.1.6 the router can optionally identify itself to the master
using a custom hostname. The specified hostname can be seen in the master via
`SHOW SLAVE HOSTS` command. The default is not to send any hostname string
during registration.

### `user`

*Note:* This is option can only be given to the `router_options` parameter. Use
 the `user` parameter of the service instead.

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

The user that is used for replication must be granted replication privileges on
the database server.

```
CREATE USER 'repl'@'maxscalehost' IDENTIFIED by 'password';
GRANT REPLICATION SLAVE ON *.* TO 'repl'@'maxscalehost';
```

### `password`

*Note:* This is option can only be given to the `router_options` parameter. Use
 the `password` parameter of the service instead.

The password for the user. If the password is not explicitly given then the
password in the service entry will be used. For compatibility with other
username and password definitions within the MariaDB MaxScale configuration file
it is also possible to use the parameter `passwd`.

### `heartbeat`

This defines the value of the heartbeat interval in seconds for the connection
to the master. The default value for the heartbeat period is every 5 minutes.

MariaDB MaxScale requests the master to ensure that a binlog event is sent at
least every heartbeat period. If there are no real binlog events to send the
master will sent a special heartbeat event. The current interval value is
reported in the diagnostic output.

### `burstsize`

This parameter is used to define the maximum amount of data that will be sent to
a slave by MariaDB MaxScale when that slave is lagging behind the master. The
default value is `1M`.

The burst size can be provided as specified
[here](../Getting-Started/Configuration-Guide.md#sizes), except that IEC binary
prefixes can be used as suffixes only from MaxScale 2.1 onwards. MaxScale 2.0
and earlier only support `burstsize` defined in bytes.

In this situation the slave is said to be in "catchup mode", this parameter is
designed to both prevent flooding of that slave and also to prevent threads
within MariaDB MaxScale spending disproportionate amounts of time with slaves
that are lagging behind the master.

### `mariadb10-compatibility`

This parameter allows binlogrouter to replicate from a MariaDB 10.0 master
server: this parameter is enabled by default since MaxScale 2.2.0.
In earlier versions the parameter was disabled by default.

```
# Example
mariadb10-compatibility=1
```


Additionally, since MaxScale 2.2.1, MariaDB 10.x slave servers
can connect to binlog server using GTID value instead of binlog name and position.

Example of a MariaDB 10.x slave connection to MaxScale

```
MariaDB> SET @@global.gtid_slave_pos='0-10122-230';
MariaDB> CHANGE MASTER TO
         MASTER_HOST='192.168.10.8',
         MASTER_PORT=5306,
         MASTER_USE_GTID=Slave_pos;
MariaDB> START SLAVE;
```

**Note:**

- Slave servers can connect either with _file_ and _pos_ or GTID.

- MaxScale saves all the incoming MariaDB GTIDs (DDLs and DMLs)
in a sqlite3 database located in _binlogdir_ (`gtid_maps.db`).
When a slave server connects with a GTID request a lookup is made for
the value match and following binlog events will be sent.


### `transaction_safety`

This parameter is used to enable/disable incomplete transactions detection in
binlog router. The default value is _off_.

When MariaDB MaxScale starts an error message may appear if current binlog file
is corrupted or an incomplete transaction is found. During normal operations
binlog events are not distributed to the slaves until a COMMIT is seen. Set
transaction_safety=on to enable detection of incomplete transactions.

### `send_slave_heartbeat`

This defines whether MariaDB MaxScale sends the heartbeat packet to the slave
when there are no real binlog events to send. This parameter takes a boolean
value and the default value is false. This means that no heartbeat events are
sent to slave servers.

If value is set to true the interval value (requested by the slave during
registration) is reported in the diagnostic output and the packet is send after
the time interval without any event to send.

### `semisync`

This parameter controls whether binlog server could ask Master server to start
the Semi-Synchronous replication. This parameter takes a boolean value and the
default value is false.

In order to get semi-sync working, the Master server must have the
*rpl_semi_sync_master* plugin installed. The availability of the plugin and the
value of the GLOBAL VARIABLE *rpl_semi_sync_master_enabled* are checked in the
Master registration phase: if the plugin is installed in the Master database,
the binlog server subsequently requests the semi-sync option.

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
master.ini or later via a _CHANGE MASTER TO_ command. This parameter cannot be
modified at runtime. The default verification depth is 9.

### `encrypt_binlog`

Whether to encrypt binlog files: the default is _off_.

When set to _on_ the binlog files will be encrypted using specified AES algorithm
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
possible to use an existing key file (not encrypted) which could contain several
`scheme;key` values: only key id with value 1 will be parsed, and if not found
an error will be reported.

Example key file with multiple keys:

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

### `mariadb10_master_gtid`

This option allows MaxScale binlog router to register with MariaDB 10.X master
using GTID instead of _binlog_file_ name and _position_ in CHANGE MASTER TO
admin command. This feature is disabled by default.

The user can set a known GTID or an empty value (in this case the Master server
will send events from it's first available binlog file).

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

If using GTID request then it's no longer possible to use MASTER_LOG_FILE and
MASTER_LOG_POS in `CHANGE MASTER TO` command: an error will be reported.

If this feature is enabled, the _transaction_safety_ option will be
automatically enabled. The binlog files will also be stored in a
hierarchical directory tree instead of a single directory.

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
user=maxscale
passwd=maxpwd
server_id=3
binlogdir=/var/lib/maxscale/
mariadb10-compatibility=1
encrypt_binlog=1
encryption_algorithm=aes_ctr
encryption_key_file=/var/binlogs/enc_key.txt
```

## Using secondary masters

From MaxScale 2.3 onwards it is possible to specify secondary masters that
the binlog router can use in case the connection to the default master fails.

**Note:** This is _only_ supported for gtid based replication in conjunction
with a Galera cluster and provided the following holds:
* `@@log_slave_updates` is enabled on all servers, and
* all nodes in the Galera cluster have the *same* `server_id`.

The initial setup is performed exactly like when there is but one default master.
```
# mysql -h $MAXSCALE_HOST -P $MAXCALE_PORT
MariaDB> SET @@global.gtid_slave_pos='0-198-123';
MariaDB> CHANGE MASTER TO
         MASTER_HOST='192.168.10.5',
         MASTER_PORT=3306,
         MASTER_USER='repl',
         MASTER_PASSWORD='repl',
         MASTER_USE_GTID=Slave_pos;
```
After the setup of the default master, secondary masters can be configured
as follows:
```
MariaDB> CHANGE MASTER ':2' TO
         MASTER_HOST='192.168.10.6',
         MASTER_PORT=3306,
         MASTER_USER='repl',
         MASTER_PASSWORD='repl',
         MASTER_USE_GTID=Slave_pos;
```
That is, a connection name must be provided and the name must be of the
format `:N` where `N` is a positive integer. If several secondary masters
are specified, they must be numbered consecutively, starting from `2`.

All settings that are not explicitly specified are copied from the
default master. That is, the following is equivalent with the command
above:
```
MariaDB> CHANGE MASTER ':2' TO MASTER_HOST='192.168.10.6';
```
If a particular master configuration exists already, then any specified
definitions will be changed and unspecified ones will remain unchanged.
For instance, the following command would only change the password of `:2`.
```
MariaDB> CHANGE MASTER ':2' TO MASTER_PASSWORD='repl2';
```
It is not possible to delete a particular secondary master, but if
`MASTER_HOST` is set on the default master, even if it is set to the same
value, then _all_ secondary master configurations are deleted.

When `START SLAVE` is issued, MaxScale will first attempt to connect to the
default master and if that fails, try the secondary masters in order, until
a connection can be created. Only if all connection attempts fail, will
MaxScale wait as specified with `connect_retry`, before doing the cycle over
again.

Once the binlog router has successfully connected to a server, it will stay
connected to that server until the connection breaks or `STOP SLAVE` is
issued.

## Examples

The [Replication Proxy](../Tutorials/Replication-Proxy-Binlog-Router-Tutorial.md) tutorial will
show you how to configure and administrate a binlogrouter installation.

Tutorial also includes SSL communication setup to the master server and SSL
client connections setup to MaxScale Binlog Server.
