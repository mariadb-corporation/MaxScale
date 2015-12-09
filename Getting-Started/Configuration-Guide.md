# MaxScale Configuration & Usage Scenarios

## Introduction

The purpose of this document is to describe how to configure MaxScale and to discuss some possible usage scenarios for MaxScale. MaxScale is designed with flexibility in mind, and consists of an event processing core with various support functions and plugin modules that tailor the behavior of the MaxScale itself.

# Table of Contents

* [Configuration](#configuration)
  * [Global Settings](#global-settings)
  * [Service](#service)
    * [Service and SSL](#service-and-ssl)
  * [Server](#server)
  * [Listener](#listener)
* [Router Modules](#routing-modules)
* [Diagnostic Modules](#diagnostic-modules)
* [Monitor Modules](#monitor-modules)
* [Filter Modules](#filter-modules)
* [Reloading Configuration](#reloading-configuration)
* [Authentication](#authentication)
* [Error Reporting](#error-reporting)

### Terms


|        Term       |    Description
------------------- | ------------------
           service | A service represents a set of databases with a specific access mechanism that is offered to clients of MaxScale. The access mechanism defines the algorithm that MaxScale will use to direct particular requests to the individual databases.
            server | A server represents an individual database server to which a client can be connected via MaxScale.
            router | A router is a module within MaxScale that will route client requests to the various database servers which MaxScale provides a service interface to.
connection routing | Connection routing is a method of handling requests in which MaxScale will accept connections from a client and route data on that connection to a single database using a single connection. Connection based routing will not examine individual requests on a connection and it will not move that connection once it is established.
statement routing  | Statement routing is a method of handling requests in which each request within a connection will be handled individually. Requests may be sent to one or more servers and connections may be dynamically added or removed from the session.
          protocol | A protocol is a module of software that is used to communicate with another software entity within the system. MaxScale supports the dynamic loading of protocol modules to allow for increased flexibility.
            module | A module is a separate code entity that may be loaded dynamically into MaxScale to increase the available functionality. Modules are implemented as run-time loadable shared objects.
           monitor | A monitor is a module that can be executed within MaxScale to monitor the state of a set of database. The use of an internal monitor is optional, monitoring may be performed externally to MaxScale.
          listener | A listener is the network endpoint that is used to listen for connections to MaxScale from the client applications. A listener is associated to a single service, however, a service may have many listeners.
connection failover| When a connection currently being used between MaxScale and the database server fails a replacement will be automatically created to another server by MaxScale without client intervention
  backend database | A term used to refer to a database that sits behind MaxScale and is accessed by applications via MaxScale.
            filter | A module that can be placed between the client and the MaxScale router module. All client data passes through the filter module and may be examined or modified by the filter modules.  Filters may be chained together to form processing pipelines.

## Configuration

The MaxScale configuration is read from a file that MaxScale will look for
in a number of places.

1. Location given with the --configdir=<path> command line argument

2. MaxScale will look for a configuration file called `maxscale.cnf` in the directory `/etc/maxscale.cnf`

An explicit path to a configuration file can be passed by using the `-f` option to MaxScale.

The configuration file itself is based on the ".ini" file format and consists of various sections that are used to build the configuration; these sections define services, servers, listeners, monitors and global settings. Parameters, which expect a comma-separated list of values can be defined on multiple lines. The following is an example of a multi-line definition.

```
[MyService]
type=service
router=readconnroute
servers=server1,
        server2,
        server3
```

The values of the parameter that are not on the first line need to have at least one whitespace character before them in order for them to be recognized as a part of the multi-line parameter.

### Global Settings

The global settings, in a section named `[MaxScale]`, allow various parameters that affect MaxScale as a whole to be tuned.

#### `threads`

This parameter controls the number of worker threads that are handling the
events coming from the kernel. MaxScale will auto-detect the number of
processors of the system unless number of threads is manually configured.
It is recommended that you let MaxScale detect how many cores the system
has and leave this parameter undefined. The number of used cores will be
logged into the message logs and if you are not satisfied with the
auto-detected value, you can manually configure it. Increasing the amount
of worker threads beyond the number of processor cores does not improve
the performance, rather is likely to degrade it, and can consume resources
needlessly.

```
# Valid options are:
#       threads=<number of epoll threads>

[MaxScale]
threads=1
```

It should be noted that additional threads will be created to execute other internal services within MaxScale. This setting is used to configure the number of threads that will be used to manage the user connections.

#### `auth_connect_timeout`

The connection timeout in seconds for the MySQL connections to the backend server when user authentication data is fetched. Increasing the value of this parameter will cause MaxScale to wait longer for a response from the backend server before aborting the authentication process.

#### `auth_read_timeout`

The read timeout in seconds for the MySQL connection to the backend database when user authentication data is fetched. Increasing the value of this parameter will cause MaxScale to wait longer for a response from the backend server when user data is being actively fetched. If the authentication is failing and you either have a large number of database users and grants or the connection to the backend servers is slow, it is a good idea to increase this value.

#### `auth_write_timeout`

The write timeout in seconds for the MySQL connection to the backend database when user authentication data is fetched. Currently MaxScale does not write or modify the data in the backend server.

#### `ms_timestamp`

Enable or disable the high precision timestamps in logfiles. Enabling this adds millisecond precision to all logfile timestamps.

```
# Valid options are:
#       ms_timestamp=<0|1>
ms_timestamp=1
```

#### `syslog`
Enable to disable to logging of messages to *syslog*.

By default logging to *syslog* is enabled.
```
# Valid options are:
#       syslog=<0|1>
syslog=1
```

To enable logging to syslog use the value 1 and to disable use
the value 0.

#### `maxlog`
Enable to disable to logging of messages to MaxScale's log file.

By default logging to *maxlog* is enabled.
```
# Valid options are:
#       syslog=<0|1>
maxlog=1
```

To enable logging to the MaxScale log file use the value 1 and to
disable use the value 0.

#### `log_to_shm`
Enable or disable the writing of the *maxscale.log* file to shared memory.
If enabled, then the actual log file will be created under `/dev/shm` and
a symbolic link to that file will be created in the *MaxScale* log directory.

Logging to shared memory may be appropriate if *log_info* and/or *log_debug*
are enabled, as logging to a regular file may in that case cause performance
degradation, due to the amount of data logged. However, as shared memory is
a scarce resource, logging to shared memory should be used only temporarily
and not regularly.

Since *MaxScale* can log to both file and *syslog* an approach that provides
maximum flexibility is to enable *syslog* and *log_to_shm*, and to disable
*maxlog*. That way messages will normally be logged to *syslog*, but if
there is something to investigate, *log_info* and *maxlog* can be enabled
from *maxadmin*, in which case informational messages will be logged to
the *maxscale.log* file that resides in shared memory.

By default, logging to shared memory is disabled.

```
# Valid options are:
#       log_to_shm=<0|1>
log_to_shm=1
```

To enable logging to shared memory use the value 1 and to disable use
the value 0.

#### `log_warning`
Enable or disable the logging of messages whose syslog priority is *warning*.
Messages of this priority are enabled by default.

```
# Valid options are:
#       log_warning=<0|1>
log_warning=0
```

To disable these messages use the value 0 and to enable them use the value 1.

#### `log_notice`
Enable or disable the logging of messages whose syslog priority is *notice*.
Messages of this priority provide information about the functioning of
MaxScale and are enabled by default.

```
# Valid options are:
#       log_notice=<0|1>
log_notice=0
```

To disable these messages use the value 0 and to enable them use the value 1.

#### `log_info`

Enable or disable the logging of messages whose syslog priority is *info*.
These messages provide detailed information about the internal workings of
MaxScale and should not, due to their frequency, be enabled, unless there
is a specific reason for that. For instance, from these messages it will be
evident, e.g., why a particular query was routed to the master instead of
to a slave. These informational messages are disabled by default.

```
# Valid options are:
#       log_info=<0|1>
log_info=1
```

To disable these messages use the value 0 and to enable them use the value 1.

#### `log_debug`

Enable or disable the logging of messages whose syslog priority is *debug*. This kind of messages are intended for development purposes and are disabled by default.

```
# Valid options are:
#       log_debug=<0|1>
log_debug=1
```

To disable these messages use the value 0 and to enable them use the value 1.

#### `log_messages`

**Deprecated** Use *log_notice* instead.

#### `log_trace`

**Deprecated** Use *log_info* instead.

#### `log_augmentation`

Enable or disable the augmentation of messages. If this is enabled, then each logged message is appended with the name of the function where the message was logged. This is primarily for development purposes and hence is disabled by default.

```
# Valid options are:
#       log_augmentation=<0|1>
log_augmentation=1
```

To disable the augmentation use the value 0 and to enable it use the value 1.

#### `logdir`

Set the directory where the logfiles are stored. The folder needs to be both readable and writable by the user running MaxScale.

```
logdir=/tmp/
```

#### `datadir`

Set the directory where the data files used by MaxScale are stored. Modules can write to this directory and for example the binlogrouter uses this folder as the default location for storing binary logs.

```
datadir=/home/user/maxscale_data/
```

#### `libdir`

Set the directory where MaxScale looks for modules. The library directory is the only directory that MaxScale uses when it searches for modules. If you have custom modules for MaxScale, make sure you have them in this folder.

```
libdir=/home/user/lib64/
```

#### `cachedir`

Configure the directory MaxScale uses to store cached data. An example of cached data is the authentication data fetched from the backend servers. MaxScale stores this in case a connection to the backend server is not possible.

```
cachedir=/tmp/maxscale_cache/
```

#### `piddir`

Configure the directory for the PID file for MaxScale. This file contains the Process ID for the running MaxScale process.

```
piddir=/tmp/maxscale_cache/
```

#### `language`

Set the folder where the errmsg.sys file is located in. MaxScale will look for the errmsg.sys file installed with MaxScale from this folder.

```
language=/home/user/lang/
```

### Service

A service represents the database service that MaxScale offers to the clients. In general a service consists of a set of backend database servers and a routing algorithm that determines how MaxScale decides to send statements or route connections to those backend servers.

A service may be considered as a virtual database server that MaxScale makes available to its clients.

Several different services may be defined using the same set of backend servers. For example a connection based routing service might be used by clients that already performed internal read/write splitting, whilst a different statement based router may be used by clients that are not written with this functionality in place. Both sets of applications could access the same data in the same databases.

A service is identified by a service name, which is the name of the configuration file section and a type parameter of service

```
[Test Service]
type=service
```

In order for MaxScale to forward any requests it must have at least one service defined within the configuration file. The definition of a service alone is not enough to allow MaxScale to forward requests however, the service is merely present to link together the other configuration elements.

#### `router`

The router parameter of a service defines the name of the router module that will be used to implement the routing algorithm between the client of MaxScale and the backend databases. Additionally routers may also be passed a comma separated list of options that are used to control the behavior of the routing algorithm. The two parameters that control the routing choice are router and router_options. The router options are specific to a particular router and are used to modify the behavior of the router. The read connection router can be passed options of master, slave or synced, an example of configuring a service to use this router and limiting the choice of servers to those in slave state would be as follows.

```
router=readconnroute
router_options=slave
```

To change the router to connect on to servers in the  master state as well as slave servers, the router options can be modified to include the master state.

```
router=readconnroute
router_options=master,slave
```

A more complete description of router options and what is available for a given router is included with the documentation of the router itself.

#### `filters`

The filters option allow a set of filters to be defined for a service; requests from the client are passed through these filters before being sent to the router for dispatch to the backend server.  The filters parameter takes one or more filter names, as defined within the filter definition section of the configuration file. Multiple filters are separated using the | character.

```
filters=counter | QLA
```

The requests pass through the filters from left to right in the order defined in the configuration parameter.

#### `servers`

The servers parameter in a service definition provides a comma separated list of the backend servers that comprise the service. The server names are those used in the name section of a block with a type parameter of server (see below).

```
servers=server1,server2,server3
```

#### `user`

The user parameter, along with the passwd parameter are used to define the credentials used to connect to the backend servers to extract the list of database users from the backend database that is used for the client authentication.

```
user=maxscale
passwd=Mhu87p2D
```

Authentication of incoming connections is performed by MaxScale itself rather than by the database server to which the client is connected. The client will authenticate itself with MaxScale, using the username, hostname and password information that MaxScale has extracted from the backend database servers. For a detailed discussion of how this impacts the authentication process please see the "Authentication" section below.

The host matching criteria is restricted to IPv4, IPv6 will be added in a future release.

Existing user configuration in the backend databases must be checked and may be updated before successful MaxScale authentication:

In order for MaxScale to obtain all the data it must be given a username it can use to connect to the database and retrieve that data. This is the parameter that gives MaxScale the username to use for this purpose.

The account used must be able to select from the mysql.user table, the following is an example showing how to create this user.

```
MariaDB [mysql]> create user 'maxscale'@'maxscalehost' identified by 'Mhu87p2D';
Query OK, 0 rows affected (0.01 sec)

MariaDB [mysql]> grant SELECT on mysql.user to 'maxscale'@'maxscalehost';
Query OK, 0 rows affected (0.00 sec)
```

Additionally, `GRANT SELECT` on the `mysql.db` table and `SHOW DATABASES` privileges are required in order to load databases name and grants suitable for database name authorization.

```
MariaDB [(none)]> GRANT SELECT ON mysql.db TO 'maxscale'@'maxscalehost';
Query OK, 0 rows affected (0.00 sec)

MariaDB [(none)]> GRANT SHOW DATABASES ON *.* TO 'maxscale'@'maxscalehost';
Query OK, 0 rows affected (0.00 sec)
```

#### `passwd`

The passwd parameter provides the password information for the above user and may be either a plain text password or it may be an encrypted password.  See the section on encrypting passwords for use in the maxscale.cnf file. This user must be capable of connecting to the backend database and executing these SQL statements to load database names and grants from the backends:

* `SELECT user, host, password,Select_priv FROM mysql.user`.
* `SELECT user, host, db FROM mysql.db`
* `SELECT * FROM INFORMATION_SCHEMA.SCHEMATA`
* `SELECT GRANTEE,PRIVILEGE_TYPE FROM INFORMATION_SCHEMA.USER_PRIVILEGES`

#### `enable_root_user`

This parameter controls the ability of the root user to connect to MaxScale and hence onwards to the backend servers via MaxScale.

The default value is `0`, disabling the ability of the root user to connect to MaxScale.

Example for enabling root user:

```
enable_root_user=1
```

Values of `on` or `true` may also be given to enable the root user and `off` or `false` may be given to disable the use of the root user.

```
enable_root_user=true
```

#### `localhost_match_wildcard_host`

This parameter enables matching of "127.0.0.1" (localhost) against "%" wildcard host for MySQL protocol authentication. The default value is `0`, so in order to authenticate a connection from the same machine as the one on which MaxScale is running, an explicit user@localhost entry will be required in the MySQL user table.

#### `version_string`

This parameter sets a custom version string that is sent in the MySQL Handshake from MaxScale to clients.

Example:

```
version_string=5.5.37-MariaDB-RWsplit
```

If not set, the default value is the server version of the embedded MySQL/MariaDB library. Example: 5.5.35-MariaDB

#### `weightby`

The weightby parameter is used in conjunction with server parameters in order to control the load balancing applied in the router in use by the service. This allows varying weights to be applied to each server to create a non-uniform distribution of the load amongst the servers.

An example of this might be to define a parameter for each server that represents the amount of resource available on the server, we could call this serversize. Every server should then have a serversize parameter set for the server.

```
serversize=10
```

The service would then have the parameter weightby set. If there are 4 servers defined in the service, serverA, serverB, serverC and serverD, with the serversize set as shown in the table below, the connections would balanced using the percentages in this table.

 Server  |serversize|% connections
---------|----------|-------------
serverA  |   10     |     18%
serverB  |   15     |     27%
serverC  |   10     |     18%
serverD  |   20     |     36%

#### `auth_all_servers`

This parameter controls whether only a single server or all of the servers are used when loading the users from the backend servers. This takes a boolean value and when enabled, creates a union of all the users and grants on all the servers.

#### `strip_db_esc`

The strip_db_esc parameter strips escape characters from database names of grants when loading the users from the backend server. Some visual database management tools automatically escape some characters and this might cause conflicts when MaxScale tries to authenticate users.

This parameter takes a boolean value and when enabled, will strip all `\` characters from the database names.

#### `optimize_wildcard`

Enabling this feature will transform wildcard grants to individual database grants. This will consume more memory but authentication in MaxScale will be done faster. The parameter takes a boolean value.

#### `retry_on_failure`

The retry_on_failure parameter controls whether MaxScale will try to restart failed services and accepts a boolean value. This functionality is enabled by default to prevent services being permanently disabled if the starting of the service failed due to a network outage. Disabling the restarting of the failed services will cause them to be permanently disabled if the services can't be started when MaxScale is started.

#### `log_auth_warnings`

Enable or disable the logging of authentication failures and warnings. This parameter takes a boolean value.

MaxScale normally suppresses warning messages about failed authentication. Enabling this option will log those messages into the message log with details about who tried to connect to MaxScale and from where.

#### `connection_timeout`

The connection_timeout parameter is used to disconnect sessions to MaxScale that have been idle for too long. The session timeouts are disabled by default. To enable them, define the timeout in seconds in the service's configuration section.

Example:

```
[Test Service]
connection_timeout=300
```

### Service and SSL

This section describes configuration parameters for services that control the SSL/TLS encryption method and the various certificate files involved in it. To enable SSL, you must configure the `ssl` parameter with either `enabled` or `required` and provide the three files for `ssl_cert`, `ssl_key` and `ssl_ca_cert`. After this, MySQL connections to this service can be encrypted with SSL.

#### `ssl`

This enables SSL connections to the service. If this parameter is set to either `required` or `enabled` and the three certificate files can be found (these are explained afterwards), then client connections will be encrypted with SSL. If the parameter is `enabled` then both SSL and non-SSL connections can connect to this service. If the parameter is set to `required` then only SSL connections can be used for this service and non-SSL connections will get an error when they try to connect to the service.

#### `ssl_key`

The SSL private key the service should use. This will be the private key that is used as the server side private key during a client-server SSL handshake. This is a required parameter for SSL enabled services.

#### `ssl_cert`

The SSL certificate the service should use. This will be the public certificate that is used as the server side certificate during a client-server SSL handshake. This is a required parameter for SSL enabled services.

#### `ssl_ca_cert`

This is the Certificate Authority file. It will be used to verify that both the client and the server certificates are valid. This is a required parameter for SSL enabled services.

### `ssl_version`

This parameter controls the level of encryption used. Accepted values are:
 * SSLv3
 * TLSv10
 * TLSv11
 * TLSv12
 * MAX   

### `ssl_cert_verification_depth`

The maximum length of the certificate authority chain that will be accepted. Accepted values are positive integers.

```
# Example
ssl_cert_verification_depth=10
```

Example SSL enabled service configuration:

```
[ReadWriteSplitService]
type=service
router=readwritesplit
servers=server1,server2,server3
user=myuser
passwd=mypasswd
ssl=required
ssl_cert=/home/markus/certs/server-cert.pem
ssl_key=/home/markus/certs/server-key.pem
ssl_ca_cert=/home/markus/certs/ca.pem
ssl_version=TLSv12
```

This configuration requires all connections to be encrypted with SSL. It also specifies that TLSv1.2 should be used as the encryption method. The paths to the server certificate files and the Certificate Authority file are also provided.

### Server

Server sections are used to define the backend database servers that can be formed into a service. A server may be a member of one or more services within MaxScale. Servers are identified by a server name which is the section name in the configuration file. Servers have a type parameter of server, plus address port and protocol parameters.

```
[server1]
type=server
address=127.0.0.1
port=3000
protocol=MySQLBackend
```

#### `address`

The IP address or hostname of the machine running the database server that is being defined. MaxScale will use this address to connect to the backend database server.

#### `port`

The port on which the database listens for incoming connections. MaxScale will use this port to connect to the database server.

#### `protocol`

The name for the protocol module to use to connect MaxScale to the database. Currently only one backend protocol is supported, the MySQLBackend module.

#### `monitoruser`

The monitor has a username and password that is used to connect to all servers for monitoring purposes, this may be overridden by supplying a monitoruser statement for each individual server

```
monitoruser=mymonitoruser
```

#### `monitorpw`

The monitor has a username and password that is used to connect to all servers for monitoring purposes, this may be overridden by supplying a monpasswd statement for the individual servers

```
monitorpw=mymonitorpasswd
```

The monpasswd parameter may be either a plain text password or it may be an encrypted password.  See the section on encrypting passwords for use in the maxscale.cnf file.

#### `persistpoolmax`

The `persistpoolmax` parameter defaults to zero but can be set to an integer value for a back end server.
If it is non zero, then when a DCB connected to a back end server is discarded by the
system, it will be held in a pool for reuse, remaining connected to the back end server.
If the number of DCBs in the pool has reached the value given by `persistpoolmax` then
any further DCB that is discarded will not be retained, but disconnected and discarded.

#### `persistmaxtime`

The `persistmaxtime` parameter defaults to zero but can be set to an integer value
indicating a number of seconds. A DCB placed in the persistent pool for a server will
only be reused if the elapsed time since it joined the pool is less than the given
value. Otherwise, the DCB will be discarded and the connection closed.

### Listener

The listener defines a port and protocol pair that is used to listen for connections to a service. A service may have multiple listeners associated with it, either to support multiple protocols or multiple ports. As with other elements of the configuration the section name is the listener name and it can be selected freely. A type parameter is used to identify the section as a listener definition. Address is optional and it allows the user to limit connections to certain interface only. Socket is also optional and used for Unix socket connections.

```
[<Listener name>]
type=listener
service=<Service name>]
protocol=[MySQLClient|HTTPD]
address=[IP|hostname]
port=<Listen port number>
socket=<Socket path>
```

#### `service`

The service to which the listener is associated. This is the name of a service that is defined elsewhere in the configuration file.

#### `protocol`

The name of the protocol module that is used for the communication between the client and MaxScale itself.

#### `address`

The address option sets the address that will be used to bind the listening socket. The address may be specified as an IP address in 'dot notation' or as a hostname. If the address option is not included in the listener definition the listener will bind to all network interfaces.

#### `port`

The port to use to listen for incoming connections to MaxScale from the clients. If the port is omitted from the configuration a default port for the protocol will be used.

#### `socket`

The `socket` option may be included in a listener definition, this configures the listener to use Unix domain sockets to listen for incoming connections. The parameter value given is the name of the socket to use.

If a socket option and an address option is given then the listener will listen on both the specific IP address and the Unix socket.

#### Available Protocols

The protocols supported by MaxScale are implemented as external modules that are loaded dynamically into the MaxScale core. They allow MaxScale to communicate in various protocols both on the client side and the backend side. Each of the protocols can be either a client protocol or a backend protocol. Client protocols are used for client-MaxScale communication and backend protocols are for MaxScale-database communication.

##### `MySQLClient`

This is the implementation of the MySQL protocol that is used by clients of MaxScale to connect to MaxScale.

##### `MySQLBackend`

The MySQLBackend protocol module is the implementation of the protocol that MaxScale uses to connect to the backend MySQL, MariaDB and Percona Server databases. This implementation is tailored for the MaxScale to MySQL Database traffic and is not a general purpose implementation of the MySQL protocol.

##### `telnetd`

The telnetd protocol module is used for connections to MaxScale itself for the purposes of creating interactive user sessions with the MaxScale instance itself. Currently this is used in conjunction with a special router implementation, the debugcli.

##### `maxscaled`

The protocol used used by the maxadmin client application in order to connect to MaxScale and access the command line interface.

##### `HTTPD`

This protocol module is currently still under development, it provides a means to create HTTP connections to MaxScale for use by web browsers or RESTful API clients.

## Routing Modules

The main task of MaxScale is to accept database connections from client applications and route the connections or the statements sent over those connections to the various services supported by MaxScale.

Currently a number of routing modules are available, these are designed for a range of different needs.

Connection based load balancing:
* [ReadConnRoute](../Routers/ReadConnRoute.md)

Read/Write aware statement based router:
* [ReadWriteSplit](../Routers/ReadWriteSplit.md)

Simple sharding on database level:
* [SchemaRouter](../Routers/SchemaRouter.md)

Binary log server:
* [Binlogrouter](../Routers/Binlogrouter.md)

## Diagnostic modules

These modules are used for diagnostic purposes and can tell about the status of MaxScale and the cluster it is monitoring.

* [MaxAdmin Module](../Routers/CLI.md)
* [Telnet Module](../Routers/Debug-CLI.md)

## Monitor Modules

Monitor modules are used by MaxScale to internally monitor the state of the backend databases in order to set the server flags for each of those servers. The router modules then use these flags to determine if the particular server is a suitable destination for routing connections for particular query classifications. The monitors are run within separate threads of MaxScale and do not affect MaxScale's routing performance.

The use of monitors is highly recommended but it is also possible to run MaxScale without a monitor module. In this case an external monitoring system which sets the status of each server via MaxAdmin is needed.

* [Mysql Monitor](../Monitors/MySQL-Monitor.md)
* [Galera Monitor](../Monitors/Galera-Monitor.md)
* [NDBCluster Monitor](../Monitors/NDB-Cluster-Monitor.md)
* [Multi-Master Monitor](../Monitors/MM-Monitor.md)

## Filter Modules

![image alt text](images/image_10.png)

Filters provide a means to manipulate or process requests as they pass through MaxScale between the client side protocol and the query router. A full explanation of each filter's functionality can be found in its documentation.

The [Filter Tutorial](../Tutorials/Filter-Tutorial.md) document shows how you can add a filter to a service and combine multiple filters in one service.

* [Query Log All (QLA) Filter](../Filters/Query-Log-All-Filter.md)
* [Regular Expression Filter](../Filters/Regex-Filter.md)
* [Tee Filter](../Filters/Tee-Filter.md)
* [Top Filter](../Filters/Top-N-Filter.md)
* [Database Firewall Filter](../Filters/Database-Firewall-Filter.md)
* [Query Redirection Filter](../Filters/Named-Server-Filter.md)
* [RabbitMQ Filter](../Filters/RabbitMQ-Filter.md)

## Reloading Configuration

The current MaxScale configuration may be updated by editing the configuration file and then forcing MaxScale to reread the configuration file. To force MaxScale to reread the configuration file, send a SIGHUP signal to the MaxScale process or execute `reload config` in the `maxadmin` client.

Some changes in configuration can not be dynamically applied and require a complete restart of MaxScale, whilst others will take some time to be applied.

### Limitations

Services that are removed via the configuration update mechanism can not be physically removed from MaxScale until there are no longer any connections using the service.

When the number of threads is decreased the threads will not actually be terminated until such time as they complete the current operation of that thread.

Monitors can not be completely removed from the running MaxScale.

## Authentication

MySQL uses username, passwords and the client host in order to authenticate a user, so a typical user would be defined as user X at host Y and would be given a password to connect. MaxScale uses exactly the same rules as MySQL when users connect to the MaxScale instance, i.e. it will check the address from which the client is connecting and treat this in exactly the same way that MySQL would. MaxScale will pull the authentication data from one of the backend servers and use this to match the incoming connections, the assumption being that all the backend servers for a particular service will share the same set of user credentials.

It is important to understand, however, that when MaxScale itself makes connections to the backend servers the backend server will see all connections as originating from the host that runs MaxScale and not the original host from which the client connected to MaxScale. Therefore the backend servers should be configured to allow connections from the MaxScale host for every user that can connect from any host. Since there is only a single password within the database server for a given host, this limits the configuration such that a given user name must have the same password for every host from which they can connect.

To clarify, if a user *X* is defined as using password *pass1* from host *a* and *pass2* from host *b* then there must be an entry in the `user` table for user *X* from the MaxScale host, say *pass1*.

This would result in rows in the user table as follows

Username|Password|Client Host
--------|--------|-----------
   X    |  pass1 | a
   X    |  pass2 | b
   X    |  pass1 | MaxScale


In this case the user *X* would be able to connect to MaxScale from host a giving the password of *pass1*. In addition MaxScale would be able to create connections for this user to the backend servers using the username *X* and password *pass1*, since the MaxScale host is also defined to have password *pass1*. User *X* would not however be able to connect from host *b* since they would need to provide the password *pass2* in order to connect to MaxScale, but then MaxScale would not be able to connect to the backends as it would also use the password *pass2* for these connections.

### Wildcard Hosts

Hostname mapping in MaxScale works in exactly the same way as for MySQL, if the wildcard is used for the host then any host other than the localhost (127.0.0.1) will match. It is important to consider that the localhost check will be performed at the MaxScale level and at the MySQL server level.

If MaxScale and the databases are on separate hosts there are two important changes in behavior to consider:

1. Clients running on the same machine as the backend database now may access the database using the wildcard entry. The localhost check between the client and MaxScale will allow the use of the wildcard, since the client is not running on the MaxScale host. Also the wildcard entry can be used on the database host as MaxScale is making that connection and it is not running on the same host as the database.

2. Clients running on the same host as MaxScale can not access the database via MaxScale using the wildcard entry since the connection to MaxScale will be from the localhost. These clients are able to access the database directly, as they will use the wildcard entry.

If MaxScale is running on the same host as one or more of the database nodes to which it is acting as a proxy then the wildcard host entries can be used to connect to MaxScale but not to connect onwards to the database running on the same node.

In all these cases the issue may be solved by adding an explicit entry for the localhost address that has the same password as the wildcard entry. This may be done using a statement as below for each of the databases that are required:

```
MariaDB [mysql]> GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, DROP ON employee.* 'user1'@'localhost' IDENTIFIED BY 'xxx';
Query OK, 0 rows affected (0.00 sec)
```

### Limitations

At the time of writing the authentication mechanism within MaxScale does not support IPV6 address matching in connections rules. This is also in line with the current protocol modules that do not support IPV6.

Wildcard address supported in the current version of MaxScale are:

192.168.3.%
192.168.%.%
192.%.%.%

and short notations

192.%
192.%.%
192.168.%

## Error Reporting

MaxScale is designed to be executed as a service, therefore all error reports, including configuration errors, are written to the MaxScale error log file. By default, MaxScale will log to a file in `/var/log/maxscale`, the only exception to this is if the log directory is not writable, in which case a message is sent to the standard error descriptor.

### Troubleshooting

MaxScale binds on TCP ports and UNIX sockets as well.

If there is a local firewall in the server where MaxScale is installed, the IP and port must be configured in order to receive connections from outside.

If the firewall is a network facility among all the involved servers, a configuration update is required as well.

Example:

```
[Galera Listener]
type=listener
address=192.168.3.33
port=4408
socket=/servers/maxscale/galera.sock
```

TCP/IP Traffic must be permitted to 192.168.3.33 port 4408

For Unix socket, the socket file path (example: `/servers/maxscale/galera.sock`) must be writable by the Unix user MaxScale runs as.
