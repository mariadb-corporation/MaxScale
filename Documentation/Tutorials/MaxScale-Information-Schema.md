# MaxInfo Plugin
The maxinfo plugin is a special router plugin similar to the one used for implementing the server side component of the MaxAdmin interface. The plugin is designed to return data regarding the internals of MariaDB MaxScale, it provides an information schema approach to monitoring the internals of MariaDB MaxScale itself.

The plugin is capable of returning data in one of two ways, either as MySQL result sets or as JSON encoded data. The choice of which mechanism used to return the data is determined by the type of the request the router receives. If a MySQL command is received then the router will return the results as a MySQL result set, if an HTTP request is received then the data will be returned as a JSON document.

# Configuration

The plugin is configured in the `maxscale.cnf` configuration file in much the same way as any other router service is configured; there must be a service section in the configuration file and also listeners defined for that service. The service does not however require any backend servers to be associated with it, or any monitors.

The service entry should define the service name, the type as service and the router module to load.
The specified user, with the password (plain or encrypted via maxpassword utility) is allowed to connect via MySQL protocol.
Currently the user can connect to maxinfo from any remote IP and to localhost as well.

```
[MaxInfo]
type=service
router=maxinfo
user=monitor
passwd=EBD2F49C3B375812A8CDEBA632ED8BBC
```

The listener section defines the protocol, port and other information needed to create a listener for the service. To listen on a port using the MySQL protocol a section as shown below should be added to the configuration file.

```
[MaxInfo Listener]
type=listener
service=MaxInfo
protocol=MariaDBClient
port=9003
```

To listen with the HTTP protocol and hence return JSON documents a section as should below is required.

```
[MaxInfo JSON Listener]
type=listener
service=MaxInfo
protocol=HTTPD
port=8003

```

If both the MySQL and JSON responses are required then a single service can be configured with both types of listener.

As with any other listeners within MariaDB MaxScale the listeners can be bound to a particular interface by use of the address= parameter. This allows the access to the maxinfo data to be limited to the localhost by adding an address=localhost parameter in the configuration file.

```
[MaxInfo Listener]
type=listener
service=MaxInfo
protocol=MariaDBClient
address=localhost
port=9003
```

# MySQL Interface to maxinfo

The maxinfo supports a small subset of SQL statements in addition to the MySQL status and ping requests. These may be used for simple monitoring of MariaDB MaxScale.

```
% mysqladmin -hmaxscale.mariadb.com -P9003 -umonitor -pxyz ping
mysqld is alive
% mysqladmin -hmaxscale.mariadb.com -P9003 -umonitor -pxyz status
Uptime: 72  Threads: 1  Sessions: 11
%
```

The SQL command used to interact with maxinfo is the show command, a variety of show commands are available and will be described in the following sections.

Maxinfo also supports the `FLUSH LOGS`, `SET SERVER <name> <status>` and `CLEAR SERVER <name> <status>` commands. These behave the same as their MaxAdmin counterpart.

## Show variables

The show variables command will display a set of name and value pairs for a number of MariaDB MaxScale system variables.

```
mysql> show variables;
+--------------------+-------------------------+
| Variable_name      | Value                   |
+--------------------+-------------------------+
| version            | 1.0.6-unstable          |
| version_comment    | MariaDB MaxScale        |
| basedir            | /home/mriddoch/skygate2 |
| MAXSCALE_VERSION   | 1.0.6-unstable          |
| MAXSCALE_THREADS   | 1                       |
| MAXSCALE_NBPOLLS   | 3                       |
| MAXSCALE_POLLSLEEP | 1000                    |
| MAXSCALE_UPTIME    | 223                     |
| MAXSCALE_SESSIONS  | 11                      |
+--------------------+-------------------------+
9 rows in set (0.02 sec)

mysql>
```

The show variables command can also accept a limited like clause. This like clause must either be a literal string to match, a pattern starting with a %, a pattern ending with a % or a string with a % at both the start and the end.

```
mysql> show variables like 'version';
+---------------+----------------+
| Variable_name | Value          |
+---------------+----------------+
| version       | 1.0.6-unstable |
+---------------+----------------+
1 row in set (0.02 sec)

mysql> show variables like 'version%';
+-----------------+------------------+
| Variable_name   | Value            |
+-----------------+------------------+
| version         | 1.0.6-unstable   |
| version_comment | MariaDB MaxScale |
+-----------------+------------------+
2 rows in set (0.02 sec)

mysql> show variables like '%comment';
+-----------------+------------------+
| Variable_name   | Value            |
+-----------------+------------------+
| version_comment | MariaDB MaxScale |
+-----------------+------------------+
1 row in set (0.02 sec)

mysql>  show variables like '%ers%';
+------------------+------------------+
| Variable_name    | Value            |
+------------------+------------------+
| version          | 1.0.6-unstable   |
| version_comment  | MariaDB MaxScale |
| MAXSCALE_VERSION | 1.0.6-unstable   |
+------------------+------------------+
3 rows in set (0.02 sec)

mysql>
```

## Show status

The show status command displays a set of status counters, as with show variables the show status command can be passed a simplified like clause to limit the values returned.

```
mysql> show status;
+---------------------------+-------+
| Variable_name             | Value |
+---------------------------+-------+
| Uptime                    | 156   |
| Uptime_since_flush_status | 156   |
| Threads_created           | 1     |
| Threads_running           | 1     |
| Threadpool_threads        | 1     |
| Threads_connected         | 11    |
| Connections               | 11    |
| Client_connections        | 2     |
| Backend_connections       | 0     |
| Listeners                 | 9     |
| Zombie_connections        | 0     |
| Internal_descriptors      | 2     |
| Read_events               | 22    |
| Write_events              | 24    |
| Hangup_events             | 0     |
| Error_events              | 0     |
| Accept_events             | 2     |
| Event_queue_length        | 1     |
| Pending_events            | 0     |
| Max_event_queue_length    | 1     |
| Max_event_queue_time      | 0     |
| Max_event_execution_time  | 0     |
+---------------------------+-------+
22 rows in set (0.02 sec)

mysql>
```

## Show services

The show services command will return a set of basic statistics regarding each of the configured services within MariaDB MaxScale.

```
mysql> show services;
+----------------+----------------+--------------+----------------+
| Service Name   | Router Module  | No. Sessions | Total Sessions |
+----------------+----------------+--------------+----------------+
| Test Service   | readconnroute  | 1            | 1              |
| Split Service  | readwritesplit | 1            | 1              |
| Filter Service | readconnroute  | 1            | 1              |
| Named Service  | readwritesplit | 1            | 1              |
| QLA Service    | readconnroute  | 1            | 1              |
| Debug Service  | debugcli       | 1            | 1              |
| CLI            | cli            | 1            | 1              |
| MaxInfo        | maxinfo        | 4            | 4              |
+----------------+----------------+--------------+----------------+
8 rows in set (0.02 sec)

mysql>
```

The show services command does not accept a like clause and will ignore any like clause that is given.

## Show listeners

The show listeners command will return a set of status information for every listener defined within the MariaDB MaxScale configuration file.

```
mysql> show listeners;
+----------------+-----------------+-----------+------+---------+
| Service Name   | Protocol Module | Address   | Port | State   |
+----------------+-----------------+-----------+------+---------+
| Test Service   | MariaDBClient   | *         | 4006 | Running |
| Split Service  | MariaDBClient   | *         | 4007 | Running |
| Filter Service | MariaDBClient   | *         | 4008 | Running |
| Named Service  | MariaDBClient   | *         | 4010 | Running |
| QLA Service    | MariaDBClient   | *         | 4009 | Running |
| Debug Service  | telnetd         | localhost | 4242 | Running |
| CLI            | maxscaled       | localhost | 6603 | Running |
| MaxInfo        | MariaDBClient   | *         | 9003 | Running |
| MaxInfo        | HTTPD           | *         | 8003 | Running |
+----------------+-----------------+-----------+------+---------+
9 rows in set (0.02 sec)

mysql>
```

The show listeners command will ignore any like clause passed to it.

## Show sessions

The show sessions command returns information on every active session within MariaDB MaxScale. It will ignore any like clause passed to it.

```
mysql> show sessions;
+-----------+---------------+----------------+---------------------------+
| Session   | Client        | Service        | State                     |
+-----------+---------------+----------------+---------------------------+
| 0x1a92a60 | 127.0.0.1     | MaxInfo        | Session ready for routing |
| 0x1a92100 | 80.240.130.35 | MaxInfo        | Session ready for routing |
| 0x1a76a00 |               | MaxInfo        | Listener Session          |
| 0x1a76020 |               | MaxInfo        | Listener Session          |
| 0x1a75d40 |               | CLI            | Listener Session          |
| 0x1a75220 |               | Debug Service  | Listener Session          |
| 0x1a774b0 |               | QLA Service    | Listener Session          |
| 0x1a78630 |               | Named Service  | Listener Session          |
| 0x1a60270 |               | Filter Service | Listener Session          |
| 0x1a606f0 |               | Split Service  | Listener Session          |
| 0x19b0380 |               | Test Service   | Listener Session          |
+-----------+---------------+----------------+---------------------------+
11 rows in set (0.02 sec)

mysql>
```

## Show clients

The show clients command reports a row for every client application connected to MariaDB MaxScale. Like clauses are not available of the show clients command.

```
mysql> show clients;
+-----------+---------------+---------+---------------------------+
| Session   | Client        | Service | State                     |
+-----------+---------------+---------+---------------------------+
| 0x1a92a60 | 127.0.0.1     | MaxInfo | Session ready for routing |
| 0x1a92100 | 80.240.130.35 | MaxInfo | Session ready for routing |
+-----------+---------------+---------+---------------------------+
2 rows in set (0.02 sec)

mysql>
```

## Show servers

The show servers command returns data for each backend server configured within the MariaDB MaxScale configuration file. This data includes the current number of connections MariaDB MaxScale has to that server and the state of that server as monitored by MariaDB MaxScale.

```
mysql> show servers;
+---------+-----------+------+-------------+---------+
| Server  | Address   | Port | Connections | Status  |
+---------+-----------+------+-------------+---------+
| server1 | 127.0.0.1 | 3306 | 0           | Running |
| server2 | 127.0.0.1 | 3307 | 0           | Down    |
| server3 | 127.0.0.1 | 3308 | 0           | Down    |
| server4 | 127.0.0.1 | 3309 | 0           | Down    |
+---------+-----------+------+-------------+---------+
4 rows in set (0.02 sec)

mysql>
```

## Show modules

The show modules command reports the information on the modules currently loaded into MariaDB MaxScale. This includes the name type and version of each module. It also includes the API version the module has been written against and the current release status of the module.

```
mysql> show modules;
+----------------+-------------+---------+-------------+----------------+
| Module Name    | Module Type | Version | API Version | Status         |
+----------------+-------------+---------+-------------+----------------+
| HTTPD          | Protocol    | V1.0.1  | 1.0.0       | In Development |
| maxscaled      | Protocol    | V1.0.0  | 1.0.0       | GA             |
| telnetd        | Protocol    | V1.0.1  | 1.0.0       | GA             |
| MariaDBClient  | Protocol    | V1.0.0  | 1.0.0       | GA             |
| mysqlmon       | Monitor     | V1.4.0  | 1.0.0       | GA             |
| readwritesplit | Router      | V1.0.2  | 1.0.0       | GA             |
| readconnroute  | Router      | V1.1.0  | 1.0.0       | GA             |
| debugcli       | Router      | V1.1.1  | 1.0.0       | GA             |
| cli            | Router      | V1.0.0  | 1.0.0       | GA             |
| maxinfo        | Router      | V1.0.0  | 1.0.0       | Alpha          |
+----------------+-------------+---------+-------------+----------------+
10 rows in set (0.02 sec)

mysql>
```

## Show monitors

The show monitors command reports each monitor configured within the system and the state of that monitor.

```
mysql> show monitors;
+---------------+---------+
| Monitor       | Status  |
+---------------+---------+
| MySQL Monitor | Running |
+---------------+---------+
1 row in set (0.02 sec)

mysql>
```

## Show eventTimes

The show eventTimes command returns a table of statistics that reflect the performance of the event queuing and execution portion of the MariaDB MaxScale core.

```
mysql> show eventTimes;
+---------------+-------------------+---------------------+
| Duration      | No. Events Queued | No. Events Executed |
+---------------+-------------------+---------------------+
| < 100ms       | 460               | 456                 |
|  100 -  200ms | 0                 | 3                   |
|  200 -  300ms | 0                 | 0                   |
|  300 -  400ms | 0                 | 0                   |
|  400 -  500ms | 0                 | 0                   |
|  500 -  600ms | 0                 | 0                   |
|  600 -  700ms | 0                 | 0                   |
|  700 -  800ms | 0                 | 0                   |
|  800 -  900ms | 0                 | 0                   |
|  900 - 1000ms | 0                 | 0                   |
| 1000 - 1100ms | 0                 | 0                   |
| 1100 - 1200ms | 0                 | 0                   |
| 1200 - 1300ms | 0                 | 0                   |
| 1300 - 1400ms | 0                 | 0                   |
| 1400 - 1500ms | 0                 | 0                   |
| 1500 - 1600ms | 0                 | 0                   |
| 1600 - 1700ms | 0                 | 0                   |
| 1700 - 1800ms | 0                 | 0                   |
| 1800 - 1900ms | 0                 | 0                   |
| 1900 - 2000ms | 0                 | 0                   |
| 2000 - 2100ms | 0                 | 0                   |
| 2100 - 2200ms | 0                 | 0                   |
| 2200 - 2300ms | 0                 | 0                   |
| 2300 - 2400ms | 0                 | 0                   |
| 2400 - 2500ms | 0                 | 0                   |
| 2500 - 2600ms | 0                 | 0                   |
| 2600 - 2700ms | 0                 | 0                   |
| 2700 - 2800ms | 0                 | 0                   |
| 2800 - 2900ms | 0                 | 0                   |
| > 3000ms      | 0                 | 0                   |
+---------------+-------------------+---------------------+
30 rows in set (0.02 sec)

mysql>
```

Each row represents a time interval, in 100ms increments, with the counts representing the number of events that were in the event queue for the length of time that row represents and the number of events that were executing of the time indicated by the row.

# JSON Interface

The simplified JSON interface takes the URL of the request made to maxinfo and maps that to a show command in the above section.

## Variables

The /variables URL will return the MariaDB MaxScale variables, these variables can not be filtered via this interface.

```
$ curl http://maxscale.mariadb.com:8003/variables
[ { "Variable_name" : "version", "Value" : "1.0.6-unstable"},
{ "Variable_name" : "version_comment", "Value" : "MariaDB MaxScale"},
{ "Variable_name" : "basedir", "Value" : "/home/mriddoch/skygate2"},
{ "Variable_name" : "MAXSCALE_VERSION", "Value" : "1.0.6-unstable"},
{ "Variable_name" : "MAXSCALE_THREADS", "Value" : 1},
{ "Variable_name" : "MAXSCALE_NBPOLLS", "Value" : 3},
{ "Variable_name" : "MAXSCALE_POLLSLEEP", "Value" : 1000},
{ "Variable_name" : "MAXSCALE_UPTIME", "Value" : 3948},
{ "Variable_name" : "MAXSCALE_SESSIONS", "Value" : 12}]
$
```

## Status

Use of the /status URI will return the status information that would normally be returned by the show status command. No filtering of the status information is available via this interface

```
$ curl http://maxscale.mariadb.com:8003/status
[ { "Variable_name" : "Uptime", "Value" : 3831},
{ "Variable_name" : "Uptime_since_flush_status", "Value" : 3831},
{ "Variable_name" : "Threads_created", "Value" : 1},
{ "Variable_name" : "Threads_running", "Value" : 1},
{ "Variable_name" : "Threadpool_threads", "Value" : 1},
{ "Variable_name" : "Threads_connected", "Value" : 12},
{ "Variable_name" : "Connections", "Value" : 12},
{ "Variable_name" : "Client_connections", "Value" : 3},
{ "Variable_name" : "Backend_connections", "Value" : 0},
{ "Variable_name" : "Listeners", "Value" : 9},
{ "Variable_name" : "Zombie_connections", "Value" : 0},
{ "Variable_name" : "Internal_descriptors", "Value" : 3},
{ "Variable_name" : "Read_events", "Value" : 469},
{ "Variable_name" : "Write_events", "Value" : 479},
{ "Variable_name" : "Hangup_events", "Value" : 12},
{ "Variable_name" : "Error_events", "Value" : 0},
{ "Variable_name" : "Accept_events", "Value" : 15},
{ "Variable_name" : "Event_queue_length", "Value" : 1},
{ "Variable_name" : "Pending_events", "Value" : 0},
{ "Variable_name" : "Max_event_queue_length", "Value" : 1},
{ "Variable_name" : "Max_event_queue_time", "Value" : 0},
{ "Variable_name" : "Max_event_execution_time", "Value" : 1}]
$
```

## Services

The /services URI returns the data regarding the services defined within the configuration of MariaDB MaxScale. Two counters are returned, the current number of sessions attached to this service and the total number connected since the service started.

```
$ curl http://maxscale.mariadb.com:8003/services
[ { "Service Name" : "Test Service", "Router Module" : "readconnroute", "No. Sessions" : 1, "Total Sessions" : 1},
{ "Service Name" : "Split Service", "Router Module" : "readwritesplit", "No. Sessions" : 1, "Total Sessions" : 1},
{ "Service Name" : "Filter Service", "Router Module" : "readconnroute", "No. Sessions" : 1, "Total Sessions" : 1},
{ "Service Name" : "Named Service", "Router Module" : "readwritesplit", "No. Sessions" : 1, "Total Sessions" : 1},
{ "Service Name" : "QLA Service", "Router Module" : "readconnroute", "No. Sessions" : 1, "Total Sessions" : 1},
{ "Service Name" : "Debug Service", "Router Module" : "debugcli", "No. Sessions" : 1, "Total Sessions" : 1},
{ "Service Name" : "CLI", "Router Module" : "cli", "No. Sessions" : 1, "Total Sessions" : 1},
{ "Service Name" : "MaxInfo", "Router Module" : "maxinfo", "No. Sessions" : 5, "Total Sessions" : 20}]
$
```

## Listeners

The /listeners URI will return a JSON array with one entry per listener, each entry is a JSON object that describes the configuration and state of that listener.

```
$ curl http://maxscale.mariadb.com:8003/listeners
[ { "Service Name" : "Test Service", "Protocol Module" : "MariaDBClient", "Address" : "*", "Port" : 4006, "State" : "Running"},
{ "Service Name" : "Split Service", "Protocol Module" : "MariaDBClient", "Address" : "*", "Port" : 4007, "State" : "Running"},
{ "Service Name" : "Filter Service", "Protocol Module" : "MariaDBClient", "Address" : "*", "Port" : 4008, "State" : "Running"},
{ "Service Name" : "Named Service", "Protocol Module" : "MariaDBClient", "Address" : "*", "Port" : 4010, "State" : "Running"},
{ "Service Name" : "QLA Service", "Protocol Module" : "MariaDBClient", "Address" : "*", "Port" : 4009, "State" : "Running"},
{ "Service Name" : "Debug Service", "Protocol Module" : "telnetd", "Address" : "localhost", "Port" : 4242, "State" : "Running"},
{ "Service Name" : "CLI", "Protocol Module" : "maxscaled", "Address" : "localhost", "Port" : 6603, "State" : "Running"},
{ "Service Name" : "MaxInfo", "Protocol Module" : "MariaDBClient", "Address" : "*", "Port" : 9003, "State" : "Running"},
{ "Service Name" : "MaxInfo", "Protocol Module" : "HTTPD", "Address" : "*", "Port" : 8003, "State" : "Running"}]
$
```

## Modules

The /modules URI returns data for each plugin that has been loaded into MariaDB MaxScale. The plugin name, type and version are returned as is the version of the plugin API that the plugin was built against and the release status of the plugin.

```
$ curl http://maxscale.mariadb.com:8003/modules
[ { "Module Name" : "HTTPD", "Module Type" : "Protocol", "Version" : "V1.0.1", "API Version" : "1.0.0", "Status" : "In Development"},
{ "Module Name" : "maxscaled", "Module Type" : "Protocol", "Version" : "V1.0.0", "API Version" : "1.0.0", "Status" : "GA"},
{ "Module Name" : "telnetd", "Module Type" : "Protocol", "Version" : "V1.0.1", "API Version" : "1.0.0", "Status" : "GA"},
{ "Module Name" : "MariaDBClient", "Module Type" : "Protocol", "Version" : "V1.0.0", "API Version" : "1.0.0", "Status" : "GA"},
{ "Module Name" : "mysqlmon", "Module Type" : "Monitor", "Version" : "V1.4.0", "API Version" : "1.0.0", "Status" : "GA"},
{ "Module Name" : "readwritesplit", "Module Type" : "Router", "Version" : "V1.0.2", "API Version" : "1.0.0", "Status" : "GA"},
{ "Module Name" : "readconnroute", "Module Type" : "Router", "Version" : "V1.1.0", "API Version" : "1.0.0", "Status" : "GA"},
{ "Module Name" : "debugcli", "Module Type" : "Router", "Version" : "V1.1.1", "API Version" : "1.0.0", "Status" : "GA"},
{ "Module Name" : "cli", "Module Type" : "Router", "Version" : "V1.0.0", "API Version" : "1.0.0", "Status" : "GA"},
{ "Module Name" : "maxinfo", "Module Type" : "Router", "Version" : "V1.0.0", "API Version" : "1.0.0", "Status" : "Alpha"}]
$
```

## Sessions

The /sessions URI returns a JSON array with an object for each active session within MariaDB MaxScale.

```
$ curl http://maxscale.mariadb.com:8003/sessions
[ { "Session" : "0x1a8e9a0", "Client" : "80.176.79.245", "Service" : "MaxInfo", "State" : "Session ready for routing"},
{ "Session" : "0x1a8e6d0", "Client" : "80.240.130.35", "Service" : "MaxInfo", "State" : "Session ready for routing"},
{ "Session" : "0x1a8ddd0", "Client" : , "Service" : "MaxInfo", "State" : "Listener Session"},
{ "Session" : "0x1a92da0", "Client" : , "Service" : "MaxInfo", "State" : "Listener Session"},
{ "Session" : "0x1a92ac0", "Client" : , "Service" : "CLI", "State" : "Listener Session"},
{ "Session" : "0x1a70e90", "Client" : , "Service" : "Debug Service", "State" : "Listener Session"},
{ "Session" : "0x1a758d0", "Client" : , "Service" : "QLA Service", "State" : "Listener Session"},
{ "Session" : "0x1a73a90", "Client" : , "Service" : "Named Service", "State" : "Listener Session"},
{ "Session" : "0x1a5c0b0", "Client" : , "Service" : "Filter Service", "State" : "Listener Session"},
{ "Session" : "0x1a5c530", "Client" : , "Service" : "Split Service", "State" : "Listener Session"},
{ "Session" : "0x19ac1c0", "Client" : , "Service" : "Test Service", "State" : "Listener Session"}]
$
```

## Clients

The /clients URI is a limited version of the /sessions, in this case it only returns an entry for a session that represents a client connection.

```
$ curl http://maxscale.mariadb.com:8003/clients
[ { "Session" : "0x1a90be0", "Client" : "80.176.79.245", "Service" : "MaxInfo", "State" : "Session ready for routing"},
{ "Session" : "0x1a8e9a0", "Client" : "127.0.0.1", "Service" : "MaxInfo", "State" : "Session ready for routing"},
{ "Session" : "0x1a8e6d0", "Client" : "80.240.130.35", "Service" : "MaxInfo", "State" : "Session ready for routing"}]
$
```

## Servers

The /servers URI is used to retrieve information for each of the servers defined within the MariaDB MaxScale configuration. This information includes the connection count and the current status as monitored by MariaDB MaxScale. The connection count is only those connections made by MariaDB MaxScale to those servers.

```
$ curl http://maxscale.mariadb.com:8003/servers
[ { "Server" : "server1", "Address" : "127.0.0.1", "Port" : 3306, "Connections" : 0, "Status" : "Running"},
{ "Server" : "server2", "Address" : "127.0.0.1", "Port" : 3307, "Connections" : 0, "Status" : "Down"},
{ "Server" : "server3", "Address" : "127.0.0.1", "Port" : 3308, "Connections" : 0, "Status" : "Down"},
{ "Server" : "server4", "Address" : "127.0.0.1", "Port" : 3309, "Connections" : 0, "Status" : "Down"}]
$
```

## Event Times

The /event/times URI returns an array of statistics that reflect the performance of the event queuing and execution portion of the MariaDB MaxScale core. Each element is an object that represents a time bucket, in 100ms increments, with the counts representing the number of events that were in the event queue for the length of time that row represents and the number of events that were executing of the time indicated by the object.

```
$ curl http://maxscale.mariadb.com:8003/event/times
[ { "Duration" : "< 100ms", "No. Events Queued" : 64, "No. Events Executed" : 63},
{ "Duration" : " 100 -  200ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : " 200 -  300ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : " 300 -  400ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : " 400 -  500ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : " 500 -  600ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : " 600 -  700ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : " 700 -  800ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : " 800 -  900ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : " 900 - 1000ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : "1000 - 1100ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : "1100 - 1200ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : "1200 - 1300ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : "1300 - 1400ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : "1400 - 1500ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : "1500 - 1600ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : "1600 - 1700ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : "1700 - 1800ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : "1800 - 1900ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : "1900 - 2000ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : "2000 - 2100ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : "2100 - 2200ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : "2200 - 2300ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : "2300 - 2400ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : "2400 - 2500ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : "2500 - 2600ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : "2600 - 2700ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : "2700 - 2800ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : "2800 - 2900ms", "No. Events Queued" : 0, "No. Events Executed" : 0},
{ "Duration" : "> 3000ms", "No. Events Queued" : 0, "No. Events Executed" : 0}]
```
