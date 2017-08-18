# MaxAdmin - Admin Interface

# The Maxscale Administrative & Monitoring Client Application

 - [Overview](#overview)
 - [Configuring MariaDB MaxScale for MaxAdmin](#configuring-mariadb-maxscale-for-maxadmin)
 - [Running MaxAdmin](#running-maxadmin)
 - [Working With Administration Interface Users](#working-with-administration-interface-users)
 - [Getting Help](#getting-help)
 - [Working with Services](#working-with-services)
 - [Working with Servers](#working-with-servers)
 - [Working with Sessions](#working-with-sessions)
 - [Descriptor Control Blocks](#descriptor-control-blocks)
 - [Working with Filters](#working-with-filters)
 - [Working with Monitors](#working-with-monitors)
 - [MariaDB MaxScale Status Commands](#maxscale-status-commands)
 - [Administration Commands](#administration-commands)
 - [Runtime Configuration Changes](#runtime-configuration-changes)
   - [Servers](#servers)
   - [Listeners](#listeners)
   - [Monitors](#monitors)
 - [Tuning MariaDB MaxScale](#tuning-mariadb-maxscale)

# Overview

MaxAdmin is a simple client interface that can be used to interact with the
MariaDB MaxScale server, it allows the display of internal MariaDB MaxScale
statistics, status and control of MariaDB MaxScale operations.

MaxAdmin supports

* Interactive user sessions

* Execution of one-off commands via command line arguments

* Execution of command scripts

# Configuring MariaDB MaxScale for MaxAdmin

In order to be able to use MaxAdmin, MariaDB MaxScale must be configured for it.

There are two ways MaxAdmin can connect to to MaxScale.

* Using a Unix domain socket.
* Using a hostname and port.

The first alternative is introduced in MaxScale 2.0 and is the secure and
recommended way. The second alternative is available for backward compatibility,
but is _insecure_ and **deprecated** and _will be removed in a future version of
MaxScale_.

An example configuration looks as follows:

```
[MaxAdmin]
type=service
router=cli

[MaxAdmin Unix Listener]
type=listener
service=MaxAdmin
protocol=maxscaled
socket=default

[MaxAdmin Inet Listener]
type=listener
service=MaxAdmin
protocol=maxscaled
address=localhost
port=6603
```

In the configuration above, two listeners are created; one listening on the
default Unix domain socket and one listening on the default port.

Which approach is used has other implications than just how the communication
between MaxAdmin and MariaDB MaxScale is handled. In the former case, the
authorization is based upon the Linux identity and in the latter case on
explicitly created user accounts that have **no** relationship to the Linux
accounts.

Note that if the socket path or port are changed, then MaxAdmin has to be
invoked with `-S` or `-P` respectively.

# Running MaxAdmin

Depending on whether MariaDB MaxScale has been configured to use Unix domain
sockets or internet sockets, MaxAdmin needs to be invoked slightly differently.

If Unix domain sockets are used, then MaxAdmin needs no additional arguments:

```
alice@host$ maxadmin
MaxAdmin>
```

The above implies that the Linux user _alice_ has been enabled to use MaxAdmin.

If internet sockets are used, then either the host, port, user or password has
to be specified explicitly:

```
alice@host$ maxadmin -u maxscale-admin
Password:
MaxScale>
```
When internet sockets are enabled, initially it is possible to connect using
the username `admin` and the password `mariadb`. These remain in effect as long
as no other users have been created. As soon as the first user is added, the use
of `admin/mariadb` as login credentials is disabled.

If Unix domain sockets are used, then initially only `root` has access. MaxAdmin
usage can subsequently be enabled for other Linux users.

The MaxAdmin client application may be run in two different modes, either as an
interactive command shell for executing commands against MariaDB MaxScale or by
passing commands on the MaxAdmin command line itself.

# Working With Administration Interface Users

Both MaxAdmin and the newly added REST API use the administrative users of
MaxScale. The network type administrative users are used by the REST API as well
as MaxAdmin when it is configured to use a network listener. Linux account type
users are only used by MaxAdmin when the UNIX Domain Socket interface is
activated.

## Administrative and Read-only Users

Administrative users can perform all operations that MaxScale offers. This
includes both read-only operations as well as operations that modify the
internal state of MaxScale or its modules.

The default user for both the network and the UNIX domain socket
interfaces is an administrative user. This user will be removed once the
first administrative user of that type is created. The default user for
the network interface is `admin` with the password `mariadb`. The default
user for the UNIX domain socket interface is `root`.

Users that can only perform read-only operations are created with `add
readonly-user` command. These users can only perform operations that fetch data
and do not modify the state of MaxScale.

To convert administrative users to read-only users, delete the old
administrative user and create it as a read-only user.

## What Users Have Been Defined?

In order to see the Linux users for whom MaxAdmin usage has been enabled and any
explicitly created accounts, use the command _show users_.

```
MaxScale> show users
Enabled Linux accounts (secure)    : alice, bob, cecil
Created network accounts (insecure): maxscale-admin
MaxScale>
```

Please note that `root` will not be shown.

## Enabling a Linux account

To enable MaxAdmin usage for a particular Linux account, use the command _enable
account_.  This command is passed a user name, which should be the same as that
of an existing Linux user.

```
MaxScale> enable account bob
```

Note that it is not checked that the provided name indeed corresponds to an
existing Linux account, so it is possible to enable an account that does not
exist yet.

Note also that it is possible to enable a Linux account irrespective of how
MaxAdmin has connected to MariaDB MaxScale. That is, the command is not
restricted to MaxAdmin users connecting over a Unix domain socket.

## Disabling a Linux account

To disable MaxAdmin usage for a particular Linux account, use the command
_disable account_.  This command is passed a user name, which should be a Linux
user for whom MaxAdmin usage earlier has been enabled.

```
MaxScale> disable account bob
```

Note also that it is possible to disable a Linux account irrespective of how
MaxAdmin has connected to MariaDB MaxScale. That is, the command is not
restricted to MaxAdmin users connecting over a Unix domain socket.

Note that it is possible to disable the current user, but that will only affect
the next attempt to use MaxAdmin. `root` cannot be removed.

## Add A New User

To add a new MaxAdmin user to be used when MaxAdmin connects over an internet
socket, use the command _add user_. This command is passed a user name and a
password.

```
MaxScale> add user maxscale-admin secretpwd
User maxscale-admin has been successfully added.
MaxScale>
```

## Delete A User

To remove a user the command _remove user_ is used and it is invoked with the
username.

```
MaxScale> remove user maxscale-admin
User maxscale-admin has been successfully removed.
MaxScale>
```

Note that it is possible to remove the current user, but that will only affect
the next attempt to use MaxAdmin. The last administrative account cannot be
removed.

## Creating Read-only Users

Currently, the `list` and `show` type commands are the only operations that
read-only users can perform.

To create a read-only network user, use the `add readonly-user` command. To
enable a local Linux account as a read-only user, use the `enable
readonly-account` command. Both administrative and read-only users can be
deleted with the `remove user` and `disable account` commands.

# Command Line Switches

The MaxAdmin command accepts a number of options. See the output of
`maxadmin --help` for more details.

## Interactive Operation

If no arguments other than the command line switches are passed to MaxAdmin it
will enter its interactive mode of operation. Users will be prompted to enter
commands with a **MaxScale>** prompt. The commands themselves are documented in
the sections later in this document. A help system is available that will give
some minimal details of the commands available.

Command history is available on platforms that support the libedit library. This
allows the use of the up and down arrow keys to recall previous commands that
have been executed by MaxAdmin. The default edit mode for the history is to
emulate *emacs* commands, the behavior of libedit may however be customized using
the .editrc file. To obtain the history of commands that have been executed use
the inbuilt history command.

In interactive mode it is possible to execute a set of commands stored in an
external file by using the source command. The command takes the argument of a
filename which should contain a set of MariaDB MaxScale commands, one per
line. These will be executed in the order they appear in the file.

## Command Line Operation

MaxAdmin can also be used to execute commands that are passed on the command
line, e.g.

```
-bash-4.1$ maxadmin -S /tmp/maxadmin.sock list services
Password:
Services.
--------------------------+----------------------+--------+---------------
Service Name              | Router Module        | #Users | Total Sessions
--------------------------+----------------------+--------+---------------
Test Service              | readconnroute        |      1 |     1
Split Service             | readwritesplit       |      1 |     1
Filter Service            | readconnroute        |      1 |     1
QLA Service               | readconnroute        |      1 |     1
Debug Service             | debugcli             |      1 |     1
CLI                       | cli                  |      2 |    27
--------------------------+----------------------+--------+---------------
-bash-4.1$
```

The single command is executed and MaxAdmin then terminates. If the -p option is
not given then MaxAdmin will prompt for a password. If a MariaDB MaxScale
command requires an argument which contains whitespace, for example a service
name, that name should be quoted. The quotes will be preserved and used in the
execution of the MariaDB MaxScale command.

```
-bash-4.1$ maxadmin show service "QLA Service"
    Password:
    Service 0x70c6a0
            Service:                QLA Service
            Router:                 readconnroute (0x7ffff0f7ae60)
            Number of router sessions:      0
            Current no. of router sessions: 0
            Number of queries forwarded:    0
            Started:                Wed Jun 25 10:08:23 2014
            Backend databases
                    127.0.0.1:3309  Protocol: MySQLBackend
                    127.0.0.1:3308  Protocol: MySQLBackend
                    127.0.0.1:3307  Protocol: MySQLBackend
                    127.0.0.1:3306  Protocol: MySQLBackend
            Users data:             0x724340
            Total connections:      1
            Currently connected:    1
    -bash-4.1$
```

Command files may be executed by redirecting them to MaxAdmin.

```
maxadmin < listall.ms
```

Another option is to use the #! mechanism to make the command file executable
from the shell. To do this add a line at the start of your command file that
contains the #! directive with the path of the MaxAdmin executable. Command
options may also be given in this line. For example to create a script file that
runs a set of list commands

```
#!/usr/bin/maxadmin
list modules
list servers
list services
list listeners
list dcbs
list sessions
list filters
```

Then simply set this file to have execute permissions and it may be run like any
other command in the Linux shell.

## The .maxadmin file

MaxAdmin supports a mechanism to set defaults for the command line switches via
a file in the home directory of the user. If a file named `.maxadmin` exists, it
will be read and parameters set according to the entries in that file.

This mechanism can be used to provide defaults to the command line options. If a
command line option is provided, it will still override the value in the
`.maxadmin` file.

The parameters than can be set are:
   * `1.4`: _hostname_, _port_, _user_ and _passwd_
   * `2.0.0` and `2.0.1`: _socket_
   * `2.0.2` onwards: _socket_, _hostname_, _port_, _user_ and _passwd_ (and as synonym _password_)

An example of a `.maxadmin` file that will alter the default socket path is:

```
socket=/somepath/maxadmin.socket
```

Note that if in `2.0.2` or later, a value for _socket_ as well as any of the
internet socket related options, such as _hostname_, is provided in `.maxadmin`,
then _socket_ takes precedence. In that case, provide at least one internet
socket related option on the command line to force MaxAdmin to use an internet
socket and thus the internet socket related options from `.maxadmin`.

The `.maxadmin` file may be made read only to protect any passwords written to
that file.

# Getting Help

A help system is available that describes the commands available via the
administration interface. To obtain a list of all commands available simply type
the command `help`.

```
Available commands:
add:
    add user - Add insecure account for using maxadmin over the network
    add server - Add a new server to a service

remove:
    remove user - Remove account for using maxadmin over the network
    remove server - Remove a server from a service or a monitor

create:
    create server - Create a new server
    create listener - Create a new listener for a service
    create monitor - Create a new monitor

destroy:
    destroy server - Destroy a server
    destroy listener - Destroy a listener
    destroy monitor - Destroy a monitor

alter:
    alter server - Alter server parameters
    alter monitor - Alter monitor parameters
    alter service - Alter service parameters
    alter maxscale - Alter maxscale parameters

set:
    set server - Set the status of a server
    set pollsleep - Set poll sleep period
    set nbpolls - Set non-blocking polls
    set log_throttling - Set the log throttling configuration

clear:
    clear server - Clear server status

disable:
    disable log-priority - Disable a logging priority
    disable sessionlog-priority - [Deprecated] Disable a logging priority for a particular session
    disable root - Disable root access
    disable syslog - Disable syslog logging
    disable maxlog - Disable MaxScale logging
    disable account - Disable Linux user

enable:
    enable log-priority - Enable a logging priority
    enable sessionlog-priority - [Deprecated] Enable a logging priority for a session
    enable root - Enable root user access to a service
    enable syslog - Enable syslog logging
    enable maxlog - Enable MaxScale logging
    enable account - Activate a Linux user account for MaxAdmin use

flush:
    flush log - Flush the content of a log file and reopen it
    flush logs - Flush the content of a log file and reopen it

list:
    list clients - List all the client connections to MaxScale
    list dcbs - List all active connections within MaxScale
    list filters - List all filters
    list listeners - List all listeners
    list modules - List all currently loaded modules
    list monitors - List all monitors
    list services - List all services
    list servers - List all servers
    list sessions - List all the active sessions within MaxScale
    list threads - List the status of the polling threads in MaxScale
    list commands - List registered commands

reload:
    reload config - Reload the configuration
    reload dbusers - Reload the database users for a service

restart:
    restart monitor - Restart a monitor
    restart service - Restart a service
    restart listener - Restart a listener

shutdown:
    shutdown maxscale - Initiate a controlled shutdown of MaxScale
    shutdown monitor - Stop a monitor
    shutdown service - Stop a service
    shutdown listener - Stop a listener

show:
    show dcbs - Show all DCBs
    show dbusers - [deprecated] Show user statistics
    show authenticators - Show authenticator diagnostics for a service
    show epoll - Show the polling system statistics
    show eventstats - Show event queue statistics
    show filter - Show filter details
    show filters - Show all filters
    show log_throttling - Show the current log throttling setting (count, window (ms), suppression (ms))
    show modules - Show all currently loaded modules
    show monitor - Show monitor details
    show monitors - Show all monitors
    show persistent - Show the persistent connection pool of a server
    show server - Show server details
    show servers - Show all servers
    show serversjson - Show all servers in JSON
    show services - Show all configured services in MaxScale
    show service - Show a single service in MaxScale
    show session - Show session details
    show sessions - Show all active sessions in MaxScale
    show tasks - Show all active housekeeper tasks in MaxScale
    show threads - Show the status of the worker threads in MaxScale
    show users - Show enabled Linux accounts
    show version - Show the MaxScale version number

sync:
    sync logs - Flush log files to disk

call:
    call command - Call module command


Type `help COMMAND` to see details of each command.
Where commands require names as arguments and these names contain
whitespace either the \ character may be used to escape the whitespace
or the name may be enclosed in double quotes ".
```

To see more details on a particular command, and a list of the sub commands of
the command, type help followed by the command name.

```
MaxScale> help list
Available options to the `list` command:

list clients - List all the client connections to MaxScale

Usage: list clients

----------------------------------------------------------------------------

list dcbs - List all active connections within MaxScale

Usage: list dcbs

----------------------------------------------------------------------------

list filters - List all filters

Usage: list filters

----------------------------------------------------------------------------

list listeners - List all listeners

Usage: list listeners

----------------------------------------------------------------------------

list modules - List all currently loaded modules

Usage: list modules

----------------------------------------------------------------------------

list monitors - List all monitors

Usage: list monitors

----------------------------------------------------------------------------

list services - List all services

Usage: list services

----------------------------------------------------------------------------

list servers - List all servers

Usage: list servers

----------------------------------------------------------------------------

list sessions - List all the active sessions within MaxScale

Usage: list sessions

----------------------------------------------------------------------------

list threads - List the status of the polling threads in MaxScale

Usage: list threads

----------------------------------------------------------------------------

list commands - List registered commands

Usage: list commands [MODULE] [COMMAND]

Parameters:
MODULE  Regular expressions for filtering module names
COMMAND Regular expressions for filtering module command names

Example: list commands my-module my-command

MaxScale>
```

# Working With Services

A service is a very important concept in MariaDB MaxScale as it defines the
mechanism by which clients interact with MariaDB MaxScale and can attached to
the backend databases. A number of commands exist that allow interaction with
the services.

## What Services Are Available?

The _list services_ command can be used to discover what services are currently
available within your MariaDB MaxScale configuration.

```
MaxScale> list services
Services.
--------------------------+-------------------+--------+----------------+-------------------
Service Name              | Router Module     | #Users | Total Sessions | Backend databases
--------------------------+-------------------+--------+----------------+-------------------
RWSplit                   | readwritesplit    |      1 |              1 | server1, server2, server3, server4
SchemaRouter              | schemarouter      |      1 |              1 | server1, server2, server3, server4
RWSplit-Hint              | readwritesplit    |      1 |              1 | server1, server2, server3, server4
ReadConn                  | readconnroute     |      1 |              1 | server1
CLI                       | cli               |      2 |              2 |
--------------------------+-------------------+--------+----------------+-------------------
MaxScale>
```

In order to determine which ports services are using then the _list listeners_
command can be used.

```
MaxScale> list listeners
Listeners.
----------------------+---------------------+--------------------+-----------------+-------+--------
Name                  | Service Name        | Protocol Module    | Address         | Port  | State
----------------------+---------------------+--------------------+-----------------+-------+--------
RWSplit-Listener      | RWSplit             | MySQLClient        | *               |  4006 | Running
SchemaRouter-Listener | SchemaRouter        | MySQLClient        | *               |  4010 | Running
RWSplit-Hint-Listener | RWSplit-Hint        | MySQLClient        | *               |  4009 | Running
ReadConn-Listener     | ReadConn            | MySQLClient        | *               |  4008 | Running
CLI-Listener          | CLI                 | maxscaled          | default         |     0 | Running
----------------------+---------------------+--------------------+-----------------+-------+--------
MaxScale>
```

## See Service Details

It is possible to see the details of an individual service using the _show
service_ command. This command should be passed the name of the service you wish
to examine as an argument. Where a service name contains spaces characters there
should either be escaped or the name placed in quotes.

```
MaxScale> show service RWSplit
	Service:                             RWSplit
	Router:                              readwritesplit
	State:                               Started

	use_sql_variables_in:      all
	slave_selection_criteria:  LEAST_CURRENT_OPERATIONS
	master_failure_mode:       fail_instantly
	max_slave_replication_lag: -1
	retry_failed_reads:        true
	strict_multi_stmt:         true
	disable_sescmd_history:    true
	max_sescmd_history:        0
	master_accept_reads:       false

	Number of router sessions:           	0
	Current no. of router sessions:      	1
	Number of queries forwarded:          	0
	Number of queries forwarded to master:	0 (0.00%)
	Number of queries forwarded to slave: 	0 (0.00%)
	Number of queries forwarded to all:   	0 (0.00%)
	Started:                             Thu Apr 20 09:45:13 2017
	Root user access:                    Disabled
	Backend databases:
		[127.0.0.1]:3000    Protocol: MySQLBackend    Name: server1
		[127.0.0.1]:3001    Protocol: MySQLBackend    Name: server2
		[127.0.0.1]:3002    Protocol: MySQLBackend    Name: server3
		[127.0.0.1]:3003    Protocol: MySQLBackend    Name: server4
	Total connections:                   1
	Currently connected:                 1
MaxScale>
```

This allows the set of backend servers defined by the service to be seen along
with the service statistics and other information.

## Examining Service Users

MariaDB MaxScale provides an authentication model by which the client
application authenticates with MariaDB MaxScale using the credentials they would
normally use to with the database itself. MariaDB MaxScale loads the user data
from one of the backend databases defined for the service. The _show dbusers_
command can be used to examine the user data held by MariaDB MaxScale.

```
MaxScale> show dbusers RWSplit
User names: @localhost @localhost.localdomain 14567USER@localhost monuser@localhost monuser@% 14609USER@localhost maxuser@localhost maxuser@% 14651USER@localhost maxtest@localhost maxtest@% 14693USER@localhost skysql@localhost skysql@% 14735USER@localhost cliuser@localhost cliuser@% repuser@localhost repuser@%
MaxScale>

```

## Reloading Service User Data

MariaDB MaxScale will automatically reload user data if there are failed
authentication requests from client applications. This reloading is rate limited
and triggered by missing entries in the MariaDB MaxScale table. If a user is
removed from the backend database user table it will not trigger removal from
the MariaDB MaxScale internal table. The reload dbusers command can be used to
force the reloading of the user table within MariaDB MaxScale.

```
MaxScale> reload dbusers RWSplit
Reloaded database users for service RWSplit.
MaxScale>
```

## Stopping A Service

It is possible to stop a service from accepting new connections by using the
_shutdown service_ command. This will not affect the connections that are
already in place for a service, but will stop any new connections from being
accepted.

```
MaxScale> shutdown service RWSplit
MaxScale>
```

## Restart A Stopped Service

A stopped service may be restarted by using the _restart service_ command.

```
MaxScale> restart service RWSplit
MaxScale>
```

# Working With Servers

The server represents each of the instances of MariaDB or MySQL that a service
may use.

## What Servers Are Configured?

The command _list servers_ can be used to display a list of all the servers
configured within MariaDB MaxScale.

```
MaxScale> list servers
Servers.
-------------------+-----------------+-------+-------------+--------------------
Server             | Address         | Port  | Connections | Status
-------------------+-----------------+-------+-------------+--------------------
server1            | 127.0.0.1       |  3000 |           0 | Master, Running
server2            | 127.0.0.1       |  3001 |           0 | Slave, Running
server3            | 127.0.0.1       |  3002 |           0 | Slave, Running
server4            | 127.0.0.1       |  3003 |           0 | Slave, Running
-------------------+-----------------+-------+-------------+--------------------
MaxScale>
```

## Server Details

It is possible to see more details regarding a given server using the _show
server_ command.

```
MaxScale> show server server2
Server 0x6501d0 (server2)
	Server:                              127.0.0.1
	Status:                              Slave, Running
	Protocol:                            MySQLBackend
	Port:                                3001
	Server Version:                      10.1.22-MariaDB
	Node Id:                             3001
	Master Id:                           3000
	Slave Ids:
	Repl Depth:                          1
	Number of connections:               0
	Current no. of conns:                0
	Current no. of operations:           0
MaxScale>
```

If the server has a non-zero value set for the server configuration item
"persistpoolmax", then additional information will be shown:

```
    Persistent pool size:            1
    Persistent measured pool size:   1
    Persistent pool max size:        10
    Persistent max time (secs):      3660
```

The distinction between pool size and measured pool size is that the first is a
counter that is updated when operations affect the persistent connections pool,
whereas the measured size is the result of checking how many persistent
connections are currently in the pool. It can be slightly different, since any
expired connections are removed during the check.

## Setting The State Of A Server

MariaDB MaxScale maintains a number of status flags for each server that is
configured. These status flags are normally maintained by the monitors but there
are two commands in the user interface that can be used to manually set these
flags; the _set server_ and _clear server_ commands.

|Flag       |Description                                                                                |
|-----------|-------------------------------------------------------------------------------------------|
|running    |The server is responding to requests, accepting connections and executing database commands|
|master     |The server is a master in a replication or it can be used for database writes              |
|slave      |The server is a replication slave or is considered as a read only database                 |
|synced     |The server is a fully fledged member of a Galera cluster                                   |
|maintenance|The server is in maintenance mode. It won't be used by services or monitored by monitors   |
|stale      |The server is a [stale master server](../Monitors/MySQL-Monitor.md)                        |

All status flags, with the exception of the maintenance flag, will be set by the
monitors that are monitoring the server. If manual control is required the
monitor should be stopped.

```
MaxScale> set server server3 maintenance
MaxScale> clear server server3 maintenance
MaxScale>
```

## Viewing the persistent pool of DCB

The DCBs that are in the pool for a particular server can be displayed (in the
format described below in the DCB section) with a command like:

```
MaxScale> show persistent server1
Number of persistent DCBs: 0
```

# Working With Sessions

The MariaDB MaxScale session represents the state within MariaDB
MaxScale. Sessions are dynamic entities and not named in the configuration file,
this means that sessions can not be easily named within the user interface. The
sessions are referenced using ID values, these are actually memory address,
however the important thing is that no two session have the same ID.

## What Sessions Are Active in MariaDB MaxScale?

There are a number of ways to find out what sessions are active, the most
comprehensive being the _list sessions_ command.

```
MaxScale> list sessions
-----------------+-----------------+----------------+--------------------------
Session          | Client          | Service        | State
-----------------+-----------------+----------------+--------------------------
10               | localhost       | CLI            | Session ready for routing
11               | ::ffff:127.0.0.1 | RWSplit        | Session ready for routing
-----------------+-----------------+----------------+--------------------------

MaxScale>
```

This will give a list of client connections.

## Display Session Details

Once the session ID has been determined using one of the above method it is
possible to determine more detail regarding a session by using the _show
session_ command.

```
MaxScale> show session 11
Session 11
	State:               Session ready for routing
	Service:             RWSplit
	Client Address:          maxuser@::ffff:127.0.0.1
	Connected:               Thu Apr 20 09:51:31 2017

	Idle:                82 seconds
MaxScale>
```

# Descriptor Control Blocks

The Descriptor Control Block or DCB is a very important entity within MariaDB
MaxScale, it represents the state of each connection within MariaDB MaxScale. A
DCB is allocated for every connection from a client, every network listener and
every connection to a backend database. Statistics for each of these connections
are maintained within these DCB’s.

As with session above the DCB’s are not named and are therefore referred to by
the use of a unique ID, the memory address of the DCB.

## Finding DCB’s

There are several ways to determine what DCB’s are active within a MariaDB
MaxScale server, the most straightforward being the _list dcbs_ command.

```
MaxScale> list dcbs
Descriptor Control Blocks
------------------+----------------------------+--------------------+----------
 DCB              | State                      | Service            | Remote
------------------+----------------------------+--------------------+----------
 0x68c0a0         | DCB for listening socket   | RWSplit            |
 0x6e23f0         | DCB for listening socket   | CLI                |
 0x691710         | DCB for listening socket   | SchemaRouter       |
 0x7fffe40130f0   | DCB in the polling loop    | CLI                | localhost
 0x6b7540         | DCB for listening socket   | RWSplit-Hint       |
 0x6cd020         | DCB for listening socket   | ReadConn           |
 0x7fffd80130f0   | DCB in the polling loop    | RWSplit            | ::ffff:127.0.0.1
 0x7fffdc014590   | DCB in the polling loop    | RWSplit            |
 0x7fffdc0148d0   | DCB in the polling loop    | RWSplit            |
 0x7fffdc014c60   | DCB in the polling loop    | RWSplit            |
 0x7fffdc014ff0   | DCB in the polling loop    | RWSplit            |
------------------+----------------------------+--------------------+----------

MaxScale>
```

A MariaDB MaxScale server that has activity on it will however have many more
DCB’s than in the example above, making it hard to find the DCB that you
require. The DCB ID is also included in a number of other command outputs,
depending on the information you have it may be easier to use other methods to
locate a particular DCB.

## DCB Of A Client Connection

To find the DCB for a particular client connection it may be best to start with
the list clients command and then look at each DCB for a particular client
address to determine the one of interest.

## DCB Details

The details of DCBs can be obtained by use of the _show dcbs_
command

```
DCB: 0x68c0a0
	DCB state:          DCB for listening socket
	Service:            RWSplit
	Role:                     Service Listener
	Statistics:
		No. of Reads:             0
		No. of Writes:            0
		No. of Buffered Writes:   0
		No. of Accepts:           1
		No. of High Water Events: 0
		No. of Low Water Events:  0
DCB: 0x7fffd80130f0
	DCB state:          DCB in the polling loop
	Service:            RWSplit
	Connected to:       ::ffff:127.0.0.1
	Username:           maxuser
	Role:                     Client Request Handler
	Statistics:
		No. of Reads:             5
		No. of Writes:            0
		No. of Buffered Writes:   6
		No. of Accepts:           0
		No. of High Water Events: 0
		No. of Low Water Events:  0
DCB: 0x7fffdc014590
	DCB state:          DCB in the polling loop
	Service:            RWSplit
	Server name/IP:     127.0.0.1
	Port number:        3000
	Protocol:           MySQLBackend
	Server status:            Master, Running
	Role:                     Backend Request Handler
	Statistics:
		No. of Reads:             4
		No. of Writes:            0
		No. of Buffered Writes:   3
		No. of Accepts:           0
		No. of High Water Events: 0
		No. of Low Water Events:  0
```

The information Username, Protocol, Server Status are not always relevant, and
will not be shown when they are null.  The time the DCB was added to the
persistent pool is only shown for a DCB that is in a persistent pool.

# Working with Filters

Filters allow the request contents and result sets from a database to be
modified for a client connection, pipelines of filters can be created between
the client connection and MariaDB MaxScale router modules.

## What Filters Are Configured?

Filters are configured in the configuration file for MariaDB MaxScale, they are
given names and may be included in the definition of a service. The _list
filters_ command can be used to determine which filters are defined.

```
MaxScale> list filters
Filters
--------------------+-----------------+----------------------------------------
Filter              | Module          | Options
--------------------+-----------------+----------------------------------------
counter             | testfilter      |
QLA                 | qlafilter       | /tmp/QueryLog
Replicate           | tee             |
QLA_BLR             | qlafilter       | /tmp/QueryLog.blr0
regex               | regexfilter     |
MySQL5.1            | regexfilter     |
top10               | topfilter       |
--------------------+-----------------+----------------------------------------
MaxScale>
```

## Retrieve Details Of A Filter Configuration

The command _show filter_ can be used to display information related to a
particular filter.

```
MaxScale> show filter QLA
Filter 0x719460 (QLA)
    Module: qlafilter
    Options:        /tmp/QueryLog
            Limit logging to connections from       127.0.0.1
            Include queries that match              select.*from.*user.*where
MaxScale>
```

## Filter Usage

The _show session_ command will include details for each of the filters in use
within a session.  First use _list sessions_ or _list clients_ to find the
session of interest and then run the _show session_ command

```
MaxScale> list sessions
-----------------+-----------------+----------------+--------------------------
Session          | Client          | Service        | State
-----------------+-----------------+----------------+--------------------------
6                | ::ffff:127.0.0.1 | RWSplit-Top    | Session ready for routing
7                | localhost       | CLI            | Session ready for routing
-----------------+-----------------+----------------+--------------------------

MaxScale> show session 6
Session 6
	State:               Session ready for routing
	Service:             RWSplit-Top
	Client Address:          maxuser@::ffff:127.0.0.1
	Connected:               Thu Apr 20 09:58:38 2017

	Idle:                9 seconds
	Filter: Top
		Report size            10
		Logging to file /tmp/top.1.
		Current Top 10:
		1 place:
			Execution time: 0.000 seconds
			SQL: show tables from information_schema
		2 place:
			Execution time: 0.000 seconds
			SQL: show databases
		3 place:
			Execution time: 0.000 seconds
			SQL: show tables
		4 place:
			Execution time: 0.000 seconds
			SQL: select @@version_comment limit 1
```

The data displayed varies from filter to filter, the example above is the top
filter. This filter prints a report of the current top queries at the time the
show session command is run.

# Working With Monitors

Monitors are used to monitor the state of databases within MariaDB MaxScale in
order to supply information to other modules, specifically the routers within
MariaDB MaxScale.

## What Monitors Are Running?

To see what monitors are running within MariaDB MaxScale use the _list monitors_
command.

```
MaxScale> list monitors
---------------------+---------------------
Monitor              | Status
---------------------+---------------------
MySQL-Monitor        | Running
---------------------+---------------------
MaxScale>
```

## Details Of A Particular Monitor

To see the details of a particular monitor use the _show monitor_ command.

```
MaxScale> show monitor MySQL-Monitor
Monitor:           0x6577e0
Name:              MySQL-Monitor
State:             Running
Sampling interval: 10000 milliseconds
Connect Timeout:   3 seconds
Read Timeout:      1 seconds
Write Timeout:     2 seconds
Monitored servers: [127.0.0.1]:3000, [127.0.0.1]:3001, [127.0.0.1]:3002, [127.0.0.1]:3003
MaxScale MonitorId:	0
Replication lag:	disabled
Detect Stale Master:	enabled
Server information

Server: server1
Server ID: 3000
Read only: OFF
Slave configured: NO
Slave IO running: NO
Slave SQL running: NO
Master ID: -1
Master binlog file:
Master binlog position: 0

Server: server2
Server ID: 3001
Read only: OFF
Slave configured: YES
Slave IO running: YES
Slave SQL running: YES
Master ID: 3000
Master binlog file: binlog.000001
Master binlog position: 435

Server: server3
Server ID: 3002
Read only: OFF
Slave configured: YES
Slave IO running: YES
Slave SQL running: YES
Master ID: 3000
Master binlog file: binlog.000001
Master binlog position: 435

Server: server4
Server ID: 3003
Read only: OFF
Slave configured: YES
Slave IO running: YES
Slave SQL running: YES
Master ID: 3000
Master binlog file: binlog.000001
Master binlog position: 435


MaxScale>
```

## Shutting Down A Monitor

A monitor may be shutdown using the _shutdown monitor_ command. This allows for
manual control of the status of servers using the _set server_ and _clear
server_ commands.

```
MaxScale> shutdown monitor MySQL-Monitor
MaxScale> list monitors
---------------------+---------------------
Monitor              | Status
---------------------+---------------------
MySQL-Monitor        | Stopped
---------------------+---------------------
MaxScale>
```

## Restarting A Monitor

A monitor that has been shutdown may be restarted using the _restart monitor_
command.

```
MaxScale> restart monitor MySQL-Monitor
MaxScale> list monitors
---------------------+---------------------
Monitor              | Status
---------------------+---------------------
MySQL-Monitor        | Running
---------------------+---------------------
MaxScale>
```

# MaxScale Status Commands

A number of commands exists that enable the internal MariaDB MaxScale status to
be revealed, these commands give an insight to how MariaDB MaxScale is using
resource internally and are used to allow the tuning process to take place.

## MariaDB MaxScale Thread Usage

MariaDB MaxScale uses a number of threads, as defined in the MariaDB MaxScale
configuration file, to execute the processing of requests received from clients
and the handling of responses. The _show threads_ command can be used to
determine what each thread is currently being used for.

```
MaxScale> show threads
Polling Threads.

Historic Thread Load Average: 1.06.
Current Thread Load Average: 0.00.
15 Minute Average: 0.10, 5 Minute Average: 0.30, 1 Minute Average: 0.67

Pending event queue length averages:
15 Minute Average: 0.00, 5 Minute Average: 0.00, 1 Minute Average: 0.00

 ID | State      | # fds  | Descriptor       | Running  | Event
----+------------+--------+------------------+----------+---------------
  0 | Polling    |        |                  |          |
  1 | Polling    |        |                  |          |
  2 | Processing |      1 | 0x6e0dd0         | <202400ms | IN|OUT
  3 | Polling    |        |                  |          |
MaxScale>
```

The resultant output returns data as to the average thread utilization for the
past minutes 5 minutes and 15 minutes. It also gives a table, with a row per
thread that shows what DCB that thread is currently processing events for, the
events it is processing and how long, to the nearest 100ms has been send
processing these events.

## The Housekeeper Tasks

Internally MariaDB MaxScale has a housekeeper thread that is used to perform
periodic tasks, it is possible to use the command show tasks to see what tasks
are outstanding within the housekeeper.

```
MaxScale> show tasks
Name                      | Type     | Frequency | Next Due
--------------------------+----------+-----------+-------------------------
Load Average              | Repeated | 10        | Thu Apr 20 10:02:26 2017
MaxScale>
```

# Administration Commands

## What Modules Are In use?

In order to determine what modules are in use, and the version and status of
those modules the _list modules_ command can be used.

```
MaxScale> list modules
Modules.
----------------+-----------------+---------+-------+-------------------------
Module Name     | Module Type     | Version | API   | Status
----------------+-----------------+---------+-------+-------------------------
qc_sqlite       | QueryClassifier | V1.0.0  | 1.1.0 | Beta
MySQLAuth       | Authenticator   | V1.1.0  | 1.1.0 | GA
MySQLClient     | Protocol        | V1.1.0  | 1.1.0 | GA
MaxAdminAuth    | Authenticator   | V2.1.0  | 1.1.0 | GA
maxscaled       | Protocol        | V2.0.0  | 1.1.0 | GA
MySQLBackendAuth| Authenticator   | V1.0.0  | 1.1.0 | GA
MySQLBackend    | Protocol        | V2.0.0  | 1.1.0 | GA
mysqlmon        | Monitor         | V1.5.0  | 3.0.0 | GA
schemarouter    | Router          | V1.0.0  | 2.0.0 | Beta
readwritesplit  | Router          | V1.1.0  | 2.0.0 | GA
topfilter       | Filter          | V1.0.1  | 2.2.0 | GA
readconnroute   | Router          | V1.1.0  | 2.0.0 | GA
cli             | Router          | V1.0.0  | 2.0.0 | GA
----------------+-----------------+---------+-------+-------------------------

MaxScale>
```

This command provides important version information for the module. Each module
has two versions; the version of the module itself and the version of the module
API that it supports. Also included in the output is the status of the module,
this may be "In Development", “Alpha”, “Beta”, “GA” or “Experimental”.

## Enabling syslog and maxlog logging

MariaDB MaxScale can log messages to syslog, to a log file or to both. The
approach can be set in the config file, but can also be changed from
maxadmin. Syslog logging is identified by *syslog* and file logging by *maxlog*.

```
MaxScale> enable syslog
MaxScale> disable maxlog
```

**NOTE** If you disable both, then you will see no messages at all.

## Rotating the log file

MariaDB MaxScale logs messages to a log file in the log directory of MariaDB
MaxScale. As the log file grows continuously, it is recommended to periodically
rotate it. When rotated, the current log file will be closed and a new one with
the *same* name opened.

To retain the earlier log entries, you need to first rename the log file and then
instruct MaxScale to rotate it.

```
$ mv maxscale.log maxscale1.log
$ # MaxScale continues to write to maxscale1.log
$ kill -SIGUSR1 <maxscale-pid>
$ # MaxScale closes the file (i.e. maxscale1.log) and reopens maxscale.log
```

There are two ways for rotating the log - *flush log maxscale* and *flush logs*;
the result is identical. The two alternatives are due to historical
reasons; earlier MariaDB MaxScale had several different log files.

```
MaxScale> flush log maxscale
MaxScale> flush logs
MaxScale>
```

## Change MariaDB MaxScale Logging Options

From version 1.3 onwards, MariaDB MaxScale has a single log file where messages
of various priority (aka severity) are logged. Consequently, you no longer
enable or disable log files but log priorities. The priorities are the same as
those of syslog and the ones that can be enabled or disabled are *debug*,
*info*, *notice* and *warning*. *Error* and any more severe messages can not be
disabled.

```
MaxScale> enable log-priority info
MaxScale> disable log-priority notice
MaxScale>
```

Please note that changes made via this interface will not persist across
restarts of MariaDB MaxScale. To make a permanent change edit the maxscale.cnf
file.

## Adjusting the Log Throttling

From 2.0 onwards, MariaDB MaxScale will throttle messages that are logged too
frequently, which typically is a sign that MaxScale encounters some error that
just keeps on repeating. The aim is to prevent the log from flooding. The
configuration specifies how many times a particular error may be logged during a
period of a specified length, before it is suppressed for a period of a
specified other length.

The current log throttling configuration can be queried with

```
MaxScale> show log_throttling
10 1000 100000
```

where the numbers are the count, the length (in milliseconds) of the period
during which the counting is made, and the length (in milliseconds) of the
period the message is subsequently suppressed.

The configuration can be set with

```
MaxScale> set log_throttling 10 1000 10000
```

where numbers are specified in the same order as in the *show* case. Setting any
of the values to 0, disables the throttling.

## Reloading The Configuration

A command, _reload config_, is available that will cause MariaDB MaxScale to
reload the maxscale.cnf configuration file. Note that not all configuration
changes are taken into effect when the configuration is reloaded. Refer to
the [Configuration Guide](../Getting-Started/Configuration-Guide.md)
for a list of parameters that can be changed with it.

## Shutting Down MariaDB MaxScale

The MariaDB MaxScale server may be shutdown using the _shutdown maxscale_
command.

```
MaxScale> shutdown maxscale
MaxScale>
```

# Runtime Configuration Changes

Starting with the 2.1 version of MaxScale, you can modify the runtime
configuration. This means that new objects (servers, listeners, monitors)
can be created, altered and removed at runtime.

## Servers

### Creating a New Server

In order to add new servers into MaxScale, they must first be created. They can
be created with the `create server` command. Any runtime configuration changes
to servers are persisted meaning that they will still be in effect even after a
restart.

```
create server - Create a new server

Usage: create server NAME HOST [PORT] [PROTOCOL] [AUTHENTICATOR] [OPTIONS]

Parameters:
NAME          Server name
HOST          Server host address
PORT          Server port (default 3306)
PROTOCOL      Server protocol (default MySQLBackend)
AUTHENTICATOR Authenticator module name (default MySQLAuth)
OPTIONS       Comma separated list of options for the authenticator

The first two parameters are required, the others are optional.

Example: create server my-db-1 192.168.0.102 3306
```

### Adding Servers to Services and Monitors

To add a server to a service or a monitor, use the `add server` command. Any
changes to the servers of a service or a monitor are persisted meaning that they
will still be in effect even after a restart.

Servers added to services will only be taken into use by new sessions. Old
sessions will only use the servers that were a part of the service when they
were created.

```
add server - Add a new server to a service

Usage: add server SERVER TARGET...

Parameters:
SERVER  The server that is added to TARGET
TARGET  List of service and/or monitor names separated by spaces

A server can be assigned to a maximum of 11 objects in one command

Example: add server my-db my-service "Cluster Monitor"
```

### Removing Servers from Services and Monitors

To remove servers from a service or a monitor, use the `remove server`
command. The same rules about server usage for services that apply to `add
server` also apply to `remove server`. The servers will only be removed from new
sessions created after the command is executed.

```
remove server - Remove a server from a service or a monitor

Usage: remove server SERVER TARGET...

Parameters:
SERVER  The server that is removed from TARGET
TARGET  List of service and/or monitor names separated by spaces

A server can be removed from a maximum of 11 objects in one command

Example: remove server my-db my-service "Cluster Monitor"
```

### Altering Servers

You can alter server parameters with the `alter server` command. Any changes to
the address or port of the server will take effect for new connections
only. Changes to other parameters will take effect immediately.

Please note that in order for SSL to be enabled for a created server, all of the
required SSL parameters (`ssl`, `ssl_key`, `ssl_cert` and `ssl_ca_cert`) must be
given in the same command.

```
alter server - Alter server parameters

Usage: alter server NAME KEY=VALUE ...

Parameters:
NAME      Server name
KEY=VALUE List of `key=value` pairs separated by spaces

This will alter an existing parameter of a server. The accepted values for KEY are:

address               Server address
port                  Server port
monuser               Monitor user for this server
monpw                 Monitor password for this server
ssl                   Enable SSL, value must be 'required'
ssl_key               Path to SSL private key
ssl_cert              Path to SSL certificate
ssl_ca_cert           Path to SSL CA certificate
ssl_version           SSL version
ssl_cert_verify_depth Certificate verification depth

To configure SSL for a newly created server, the 'ssl', 'ssl_cert',
'ssl_key' and 'ssl_ca_cert' parameters must be given at the same time.

Example: alter server my-db-1 address=192.168.0.202 port=3307
```

### Destroying Servers

You can destroy created servers with the `destroy server` command. Only servers
created with the `create server` command should be destroyed. A server can only
be destroyed once it has been removed from all services and monitors.

```
destroy server - Destroy a server

Usage: destroy server NAME

Parameters:
NAME Server to destroy

Example: destroy server my-db-1
```

## Listeners

### Creating New Listeners

To create a new listener for a service, use the `create listener` command. This
will create and start a new listener for a service which will immediately start
listening for new connections on the specified port.

Please note that in order for SSL to be enabled for a created listeners, all of
the required SSL parameters (`ssl`, `ssl_key`, `ssl_cert` and `ssl_ca_cert`)
must be given. All the `create listener` parameters do not need to be defined in
order for SSL to be enabled. The _default_ parameter can be used to signal that
MaxScale should use a default value for the parameter in question.

```
create listener - Create a new listener for a service

Usage: create listener SERVICE NAME [HOST] [PORT] [PROTOCOL] [AUTHENTICATOR] [OPTIONS]
                       [SSL_KEY] [SSL_CERT] [SSL_CA] [SSL_VERSION] [SSL_VERIFY_DEPTH]

Parameters
SERVICE       Service where this listener is added
NAME          Listener name
HOST          Listener host address (default [::])
PORT          Listener port (default 3306)
PROTOCOL      Listener protocol (default MySQLClient)
AUTHENTICATOR Authenticator module name (default MySQLAuth)
OPTIONS       Options for the authenticator module
SSL_KEY       Path to SSL private key
SSL_CERT      Path to SSL certificate
SSL_CA        Path to CA certificate
SSL_VERSION   SSL version (default MAX)
SSL_VERIFY_DEPTH Certificate verification depth

The first two parameters are required, the others are optional.
Any of the optional parameters can also have the value 'default'
which will be replaced with the default value.

Example: create listener my-service my-new-listener 192.168.0.101 4006
```

### Destroying Listeners

You can destroy created listeners with the `destroy listener` command. This will
remove the persisted configuration and it will not be created on the next
startup. The listener is stopped but it will remain a part of the runtime
configuration until the next restart.

```
destroy listener - Destroy a listener

Usage: destroy listener SERVICE NAME

Parameters:
NAME Listener to destroy

The listener is stopped and it will be removed on the next restart of MaxScale

Example: destroy listener my-listener
```

## Monitors

### Creating New Monitors

The `create monitor` command creates a new monitor that is initially
stopped. Configure the monitor with the `alter monitor` command and then start
it with the `restart monitor` command. The _user_ and _password_ parameters of
the monitor must be defined before the monitor is started.

```
create monitor - Create a new monitor

Usage: create monitor NAME MODULE

Parameters:
NAME    Monitor name
MODULE  Monitor module

Example: create monitor my-monitor mysqlmon
```

### Altering Monitors

To alter a monitor, use the `alter monitor` command. Module specific parameters
can also be altered.

```
alter monitor - Alter monitor parameters

Usage: alter monitor NAME KEY=VALUE ...

Parameters:
NAME      Monitor name
KEY=VALUE List of `key=value` pairs separated by spaces

All monitors support the following values for KEY:
user                    Username used when connecting to servers
password                Password used when connecting to servers
monitor_interval        Monitoring interval in milliseconds
backend_connect_timeout Server coneection timeout in seconds
backend_write_timeout   Server write timeout in seconds
backend_read_timeout    Server read timeout in seconds

This will alter an existing parameter of a monitor. To remove parameters,
pass an empty value for a key e.g. 'maxadmin alter monitor my-monitor my-key='

Example: alter monitor my-monitor user=maxuser password=maxpwd
```

### Destroying Monitors

To destroy a monitor, use the `destroy monitor` command. All servers need to be
removed from the monitor before it can be destroyed. Only created monitors
should be destroyed and they will remain a part of the runtime configuration
until the next restart.

```
destroy monitor - Destroy a monitor

Usage: destroy monitor NAME

Parameters:
NAME Monitor to destroy

The monitor is stopped and it will be removed on the next restart of MaxScale

Example: destroy monitor my-monitor
```

## Services

### Altering Services

To alter the common service parameters, use the `alter service` command. Module
specific parameters cannot be altered with this command.

```
alter service - Alter service parameters

Usage: alter service NAME KEY=VALUE ...

Parameters:
NAME      Service name
KEY=VALUE List of `key=value` pairs separated by spaces

All services support the following values for KEY:
user                          Username used when connecting to servers
password                      Password used when connecting to servers
enable_root_user              Allow root user access through this service
max_retry_interval            Maximum restart retry interval
max_connections               Maximum connection limit
connection_timeout            Client idle timeout in seconds
auth_all_servers              Retrieve authentication data from all servers
strip_db_esc                  Strip escape characters from database names
localhost_match_wildcard_host Match wildcard host to 'localhost' address
version_string                The version string given to client connections
weightby                      Weighting parameter name
log_auth_warnings             Log authentication warnings
retry_on_failure              Retry service start on failure

Example: alter service my-service user=maxuser password=maxpwd
```

## MaxScale Core

### Altering MaxScale

The core MaxScale parameters that can be modified at runtime can be altered with
the `alter maxscale` command.

```
alter maxscale - Alter maxscale parameters

Usage: alter maxscale KEY=VALUE ...

Parameters:
KEY=VALUE List of `key=value` pairs separated by spaces

The following configuration values can be altered:
auth_connect_timeout         Connection timeout for permission checks
auth_read_timeout            Read timeout for permission checks
auth_write_timeout           Write timeout for permission checks
admin_auth                   Enable admin interface authentication

Example: alter maxscale auth_connect_timeout=10
```

## Other Modules

Modules can implement custom commands called _module commands_. These are
intended to allow modules to perform very specific tasks.

To list all module commands, execute `list commands` in maxadmin. This shows all
commands that the modules have exposed. It also explains what they do and what
sort of arguments they take.

```
list commands - List registered commands

Usage: list commands [MODULE] [COMMAND]

Parameters:
MODULE  Regular expressions for filtering module names
COMMAND Regular expressions for filtering module command names

Example: list commands my-module my-command
```

If no module commands are registered, no output will be generated. Refer to the
module specific documentation for more details about module commands.

To call a module commands, execute `call command <module> <command>` in
maxadmin. The _<module>_ is the name of the module and _<command>_ is the
command that should be called. The commands take a variable amount of arguments
which are explained in the output of `list commands`.

```
call command - Call module command

Usage: call command MODULE COMMAND ARGS...

Parameters:
MODULE  The module name
COMMAND The command to call
ARGS... Arguments for the command

To list all registered commands, run 'list commands'.

Example: call command my-module my-command hello world!
```

An example of this is the `dbfwfilter` module that implements a rule reloading
mechanism as a module command. This command takes a filter name as a parameter.

```
maxadmin call command dbfwfilter rules/reload my-firewall-filter /home/user/rules.txt
```

Here the name of the filter is _my-firewall-filter_ and the optional rule file
path is `/home/user/rules.txt`.

# Tuning MariaDB MaxScale

The way that MariaDB MaxScale does its polling is that each of the polling
threads, as defined by the threads parameter in the configuration file, will
call epoll_wait to obtain the events that are to be processed. The events are
then added to a queue for execution. Any thread can read from this queue, not
just the thread that added the event.

Once the thread has done an epoll call with no timeout it will either do an
epoll_wait call with a timeout or it will take an event from the queue if there
is one. These two new parameters affect this behavior.

The first parameter, which may be set by using the non_blocking_polls option in
the configuration file, controls the number of epoll_wait calls that will be
issued without a timeout before MariaDB MaxScale will make a call with a timeout
value. The advantage of performing a call without a timeout is that the kernel
treats this case as different and will not rescheduled the process in this
case. If a timeout is passed then the system call will cause the MariaDB
MaxScale thread to be put back in the scheduling queue and may result in lost
CPU time to MariaDB MaxScale. Setting the value of this parameter too high will
cause MariaDB MaxScale to consume a lot of CPU when there is infrequent work to
be done. The default value of this parameter is 3.

This parameter may also be set via the maxadmin client using the command _set
nbpolls <number>_.

The second parameter is the maximum sleep value that MariaDB MaxScale will pass
to epoll_wait. What normally happens is that MariaDB MaxScale will do an
epoll_wait call with a sleep value that is 10% of the maximum, each time the
returns and there is no more work to be done MariaDB MaxScale will increase this
percentage by 10%. This will continue until the maximum value is reached or
until there is some work to be done. Once the thread finds some work to be done
it will reset the sleep time it uses to 10% of the maximum.

The maximum sleep time is set in milliseconds and can be placed in the
[maxscale] section of the configuration file with the poll_sleep
parameter. Alternatively it may be set in the maxadmin client using the command
_set pollsleep <number>_. The default value of this parameter is 1000.

Setting this value too high means that if a thread collects a large number of
events and adds to the event queue, the other threads might not return from the
epoll_wait calls they are running for some time resulting in less overall
performance. Setting the sleep time too low will cause MariaDB MaxScale to wake
up too often and consume CPU time when there is no work to be done.

The _show epoll_ command can be used to see how often we actually poll with a
timeout, the first two values output are significant. Also the "Number of wake
with pending events" is a good measure. This is the count of the number of times
a blocking call returned to find there was some work waiting from another
thread. If the value is increasing rapidly reducing the maximum sleep value and
increasing the number of non-blocking polls should help the situation.

```
MaxScale> show epoll

Poll Statistics.

No. of epoll cycles:                           343
No. of epoll cycles with wait:                 66
No. of epoll calls returning events:           19
No. of non-blocking calls returning events:    10
No. of read events:                            2
No. of write events:                           15
No. of error events:                           0
No. of hangup events:                          0
No. of accept events:                          4
No. of times no threads polling:               4
Total event queue length:                      1
Average event queue length:                    1
Maximum event queue length:                    1
No of poll completions with descriptors
	No. of descriptors	No. of poll completions.
	 1			19
	 2			0
	 3			0
	 4			0
	 5			0
	 6			0
	 7			0
	 8			0
	 9			0
	>= 10			0
MaxScale>
```

If the "Number of DCBs with pending events" grows rapidly it is an indication
that MariaDB MaxScale needs more threads to be able to keep up with the load it
is under.

The _show threads_ command can be used to see the historic average for the
pending events queue, it gives 15 minute, 5 minute and 1 minute averages. The
load average it displays is the event count per poll cycle data. An idea load is
1, in this case MariaDB MaxScale threads and fully occupied but nothing is
waiting for threads to become available for processing.

The _show eventstats_ command can be used to see statistics about how long
events have been queued before processing takes place and also how long the
events took to execute once they have been allocated a thread to run on.

```
MaxScale> show eventstats

Event statistics.
Maximum queue time:             000ms
Maximum execution time:         000ms
Maximum event queue length:     1
Total event queue length:       4
Average event queue length:     1

               |    Number of events
Duration       | Queued     | Executed
---------------+------------+-----------
 < 100ms       | 27         | 26
  100 -  200ms | 0          | 0
  200 -  300ms | 0          | 0
  300 -  400ms | 0          | 0
  400 -  500ms | 0          | 0
  500 -  600ms | 0          | 0
  600 -  700ms | 0          | 0
  700 -  800ms | 0          | 0
  800 -  900ms | 0          | 0
  900 - 1000ms | 0          | 0
 1000 - 1100ms | 0          | 0
 1100 - 1200ms | 0          | 0
 1200 - 1300ms | 0          | 0
 1300 - 1400ms | 0          | 0
 1400 - 1500ms | 0          | 0
 1500 - 1600ms | 0          | 0
 1600 - 1700ms | 0          | 0
 1700 - 1800ms | 0          | 0
 1800 - 1900ms | 0          | 0
 1900 - 2000ms | 0          | 0
 2000 - 2100ms | 0          | 0
 2100 - 2200ms | 0          | 0
 2200 - 2300ms | 0          | 0
 2300 - 2400ms | 0          | 0
 2400 - 2500ms | 0          | 0
 2500 - 2600ms | 0          | 0
 2600 - 2700ms | 0          | 0
 2700 - 2800ms | 0          | 0
 2800 - 2900ms | 0          | 0
 2900 - 3000ms | 0          | 0
 > 3000ms      | 0          | 0
MaxScale>
```

The statics are defined in 100ms buckets, with the count of the events that fell
into that bucket being recorded.
