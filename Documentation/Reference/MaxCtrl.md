# MaxCtrl

MaxCtrl is a command line administrative client for MaxScale which uses
the MaxScale REST API for communication. It is intended to be the
replacement software for the legacy MaxAdmin command line client.

By default, the MaxScale REST API listens on port 8989 on the local host. The
default credentials for the REST API are `admin:mariadb`. The users used by the
REST API are the same that are used by the MaxAdmin network interface. This
means that any users created for the MaxAdmin network interface should work with
the MaxScale REST API and MaxCtrl.

For more information about the MaxScale REST API, refer to the
[REST API documentation](../REST-API/API.md) and the
[Configuration Guide](../Getting-Started/Configuration-Guide.md).

# Commands

* [list](#list)
* [show](#show)
* [set](#set)
* [clear](#clear)
* [drain](#drain)
* [enable](#enable)
* [disable](#disable)
* [create](#create)
* [destroy](#destroy)
* [link](#link)
* [unlink](#unlink)
* [start](#start)
* [stop](#stop)
* [alter](#alter)
* [rotate](#rotate)
* [call](#call)
* [cluster](#cluster)
* [api](#api)
* [classify](#classify)

## Options

All command accept the following global options.

```
  -u, --user      Username to use                    [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, give -p
                  as the last argument or use --password=''
                                                   [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format
                  and each value must be separated by a comma.
                                            [string] [default: "localhost:8989"]
  -t, --timeout   Request timeout in milliseconds      [number] [default: 10000]
  -q, --quiet     Silence all output                  [boolean] [default: false]
  --tsv           Print tab separated output          [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure              Enable HTTPS requests     [boolean] [default: false]
  --tls-key                 Path to TLS private key                     [string]
  --tls-cert                Path to TLS public certificate              [string]
  --tls-ca-cert             Path to TLS CA certificate                  [string]
  --tls-verify-server-cert  Whether to verify server TLS certificates
                                                       [boolean] [default: true]

Options:
  --version         Show version number                                [boolean]
  --tls-passphrase  Password for the TLS private key                    [string]
  --help            Show help                                          [boolean]

If no commands are given, maxctrl is started in interactive mode. Use `exit` to
exit the interactive mode.
```

## list

```
Usage: list <command>

Commands:
  servers              List servers
  services             List services
  listeners <service>  List listeners of a service
  monitors             List monitors
  sessions             List sessions
  filters              List filters
  modules              List loaded modules
  threads              List threads
  users                List created network users
  commands             List module commands

```

### list servers

`Usage: list servers`

List all servers in MaxScale.

### list services

`Usage: list services`

List all services and the servers they use.

### list listeners

`Usage: list listeners <service>`

List listeners for a service.

### list monitors

`Usage: list monitors`

List all monitors in MaxScale.

### list sessions

`Usage: list sessions`

List all client sessions.

### list filters

`Usage: list filters`

List all filters in MaxScale.

### list modules

`Usage: list modules`

List all currently loaded modules.

### list threads

`Usage: list threads`

List all worker threads.

### list users

`Usage: list users`

List the users that can be used to connect to the MaxScale REST API.

### list commands

`Usage: list commands`

List all available module commands.

## show

```
Usage: show <command>

Commands:
  server <server>    Show server
  servers            Show all servers
  service <service>  Show service
  services           Show all services
  monitor <monitor>  Show monitor
  monitors           Show all monitors
  session <session>  Show session
  sessions           Show all sessions
  filter <filter>    Show filter
  filters            Show all filters
  module <module>    Show loaded module
  modules            Show all loaded modules
  maxscale           Show MaxScale information
  thread <thread>    Show thread
  threads            Show all threads
  logging            Show MaxScale logging information
  commands <module>  Show module commands of a module

```

### show server

`Usage: show server <server>`

Show detailed information about a server. The `Parameters` field contains the
currently configured parameters for this server. See `help alter server` for
more details about altering server parameters.

### show servers

`Usage: show servers`

Show detailed information about all servers.

### show service

`Usage: show service <service>`

Show detailed information about a service. The `Parameters` field contains the
currently configured parameters for this service. See `help alter service` for
more details about altering service parameters.

### show services

`Usage: show services`

Show detailed information about all services.

### show monitor

`Usage: show monitor <monitor>`

Show detailed information about a monitor. The `Parameters` field contains the
currently configured parameters for this monitor. See `help alter monitor` for
more details about altering monitor parameters.

### show monitors

`Usage: show monitors`

Show detailed information about all monitors.

### show session

`Usage: show session <session>`

Show detailed information about a single session. The list of sessions can be
retrieved with the `list sessions` command. The <session> is the session ID of a
particular session.

The `Connections` field lists the servers to which the session is connected and
the `Connection IDs` field lists the IDs for those connections.

### show sessions

`Usage: show sessions`

Show detailed information about all sessions. See `help show session` for more
details.

### show filter

`Usage: show filter <filter>`

The list of services that use this filter is show in the `Services` field.

### show filters

`Usage: show filters`

Show detailed information of all filters.

### show module

`Usage: show module <module>`

This command shows all available parameters as well as detailed version
information of a loaded module.

### show modules

`Usage: show modules`

Displays detailed information about all modules.

### show maxscale

`Usage: show maxscale`

See `help alter maxscale` for more details about altering MaxScale parameters.

### show thread

`Usage: show thread <thread>`

Show detailed information about a worker thread.

### show threads

`Usage: show threads`

Show detailed information about all worker threads.

### show logging

`Usage: show logging`

See `help alter logging` for more details about altering logging parameters.

### show commands

`Usage: show commands <module>`

This command shows the parameters the command expects with the parameter
descriptions.

## set

```
Usage: set <command>

Commands:
  server <server> <state>  Set server state

```

### set server

`Usage: set server <server> <state>`

If <server> is monitored by a monitor, this command should only be used to set
the server into the `maintenance` state. Any other states will be overridden by
the monitor on the next monitoring interval. To manually control server states,
use the `stop monitor <name>` command to stop the monitor before setting the
server states manually.

## clear

```
Usage: clear <command>

Commands:
  server <server> <state>  Clear server state

```

### clear server

`Usage: clear server <server> <state>`

This command clears a server state set by the `set server <server> <state>`
command

## drain

```
Usage: drain <command>

Commands:
  server <server>  Drain a server of connections

Drain options:
  --drain-timeout  Timeout for the drain operation in seconds. If exceeded, the
                   server is added back to all services without putting it into
                   maintenance mode.                      [number] [default: 90]

```

### drain server

`Usage: drain server <server>`

This command drains the server of connections by first removing it from all
services after which it waits until all connections are closed. When all
connections are closed, the server is put into the `maintenance` state and added
back to all the services where it was removed from. To take the server back into
use, execute `clear server <server> maintenance`.

## enable

```
Usage: enable <command>

Commands:
  log-priority <log>  Enable log priority [warning|notice|info|debug]
  account <name>      Activate a Linux user account for administrative use

Enable account options:
  --type  Type of user to create
                         [string] [choices: "admin", "basic"] [default: "basic"]

```

### enable log-priority

`Usage: enable log-priority <log>`

The `debug` log priority is only available for debug builds of MaxScale.

### enable account

`Usage: enable account <name>`

The Linux user accounts are used by the MaxAdmin UNIX Domain Socket interface

## disable

```
Usage: disable <command>

Commands:
  log-priority <log>  Disable log priority [warning|notice|info|debug]
  account <name>      Disable a Linux user account from administrative use

```

### disable log-priority

`Usage: disable log-priority <log>`

The `debug` log priority is only available for debug builds of MaxScale.

### disable account

`Usage: disable account <name>`

The Linux user accounts are used by the MaxAdmin UNIX Domain Socket interface

## create

```
Usage: create <command>

Commands:
  server <name> <host> <port>          Create a new server
  monitor <name> <module> [params...]  Create a new monitor
  service <name> <router> <params...>  Create a new service
  filter <name> <module> [params...]   Create a new filter
  listener <service> <name> <port>     Create a new listener
  user <name> <password>               Create a new network user

Common create options:
  --protocol               Protocol module name                         [string]
  --authenticator          Authenticator module name                    [string]
  --authenticator-options  Option string for the authenticator          [string]
  --tls-key                Path to TLS key                              [string]
  --tls-cert               Path to TLS certificate                      [string]
  --tls-ca-cert            Path to TLS CA certificate                   [string]
  --tls-version            TLS version to use                           [string]
  --tls-cert-verify-depth  TLS certificate verification depth           [string]

Create server options:
  --services  Link the created server to these services                  [array]
  --monitors  Link the created server to these monitors                  [array]

Create monitor options:
  --servers           Link the created service to these servers          [array]
  --monitor-user      Username for the monitor user                     [string]
  --monitor-password  Password for the monitor user                     [string]

Create service options:
  --servers  Link the created service to these servers                   [array]
  --filters  Link the created service to these filters                   [array]

Create listener options:
  --interface  Interface to listen on                   [string] [default: "::"]

Create user options:
  --type  Type of user to create
                         [string] [choices: "admin", "basic"] [default: "basic"]

```

### create server

`Usage: create server <name> <host> <port>`

The created server will not be used by any services or monitors unless the
--services or --monitors options are given. The list of servers a service or a
monitor uses can be altered with the `link` and `unlink` commands.

### create monitor

`Usage: create monitor <name> <module> [params...]`

The list of servers given with the --servers option should not contain any
servers that are already monitored by another monitor. The last argument to this
command is a list of key=value parameters given as the monitor parameters.

### create service

`Usage: service <name> <router> <params...>`

The last argument to this command is a list of key=value parameters given as the
service parameters. If the --servers or --filters options are used, they must be
defined after the service parameters.

### create filter

`Usage: filter <name> <module> [params...]`

The last argument to this command is a list of key=value parameters given as the
filter parameters.

### create listener

`Usage: create listener <service> <name> <port>`

The new listener will be taken into use immediately.

### create user

`Usage: create user <name> <password>`

The created user can be used with the MaxScale REST API as well as the MaxAdmin
network interface. By default the created user will have read-only privileges.
To make the user an administrative user, use the `--type=admin` option.

## destroy

```
Usage: destroy <command>

Commands:
  server <name>              Destroy an unused server
  monitor <name>             Destroy an unused monitor
  listener <service> <name>  Destroy an unused listener
  service <name>             Destroy an unused service
  filter <name>              Destroy an unused filter
  user <name>                Remove a network user

```

### destroy server

`Usage: destroy server <name>`

The server must be unlinked from all services and monitor before it can be
destroyed.

### destroy monitor

`Usage: destroy monitor <name>`

The monitor must be unlinked from all servers before it can be destroyed.

### destroy listener

`Usage: destroy listener <service> <name>`

Destroying a listener closes the listening socket, opening it up for reuse.

### destroy service

`Usage: destroy service <name>`

The service must be unlinked from all servers and filters. All listeners for the
service must be destroyed before the service itself can be destroyed.

### destroy filter

`Usage: destroy filter <name>`

The filter must not be used by any service when it is destroyed.

### destroy user

`Usage: destroy user <name>`

The last remaining administrative user cannot be removed. Create a replacement
administrative user before attempting to remove the last administrative user.

## link

```
Usage: link <command>

Commands:
  service <name> <server...>  Link servers to a service
  monitor <name> <server...>  Link servers to a monitor

```

### link service

`Usage: link service <name> <server...>`

This command links servers to a service, making them available for any
connections that use the service. Before a server is linked to a service, it
should be linked to a monitor so that the server state is up to date. Newly
linked server are only available to new connections, existing connections will
use the old list of servers.

### link monitor

`Usage: link monitor <name> <server...>`

Linking a server to a monitor will add it to the list of servers that are
monitored by that monitor. A server can be monitored by only one monitor at a
time.

## unlink

```
Usage: unlink <command>

Commands:
  service <name> <server...>  Unlink servers from a service
  monitor <name> <server...>  Unlink servers from a monitor

```

### unlink service

`Usage: unlink service <name> <server...>`

This command unlinks servers from a service, removing them from the list of
available servers for that service. New connections to the service will not use
the unlinked servers but existing connections can still use the servers.

### unlink monitor

`Usage: unlink monitor <name> <server...>`

This command unlinks servers from a monitor, removing them from the list of
monitored servers. The servers will be left in their current state when they are
unlinked from a monitor.

## start

```
Usage: start <command>

Commands:
  service <name>  Start a service
  monitor <name>  Start a monitor
  maxscale        Start MaxScale by starting all services

```

### start service

`Usage: start service <name>`

This starts a service stopped by `stop service <name>`

### start monitor

`Usage: start monitor <name>`

This starts a monitor stopped by `stop monitor <name>`

### start maxscale

`Usage: start maxscale`

This command will execute the `start service` command for all services in
MaxScale.

## stop

```
Usage: stop <command>

Commands:
  service <name>  Stop a service
  monitor <name>  Stop a monitor
  maxscale        Stop MaxScale by stopping all services

```

### stop service

`Usage: stop service <name>`

Stopping a service will prevent all the listeners for that service from
accepting new connections. Existing connections will still be handled normally
until they are closed.

### stop monitor

`Usage: stop monitor <name>`

Stopping a monitor will pause the monitoring of the servers. This can be used to
manually control server states with the `set server` command.

### stop maxscale

`Usage: stop maxscale`

This command will execute the `stop service` command for all services in
MaxScale.

## alter

```
Usage: alter <command>

Commands:
  server <server> <key> <value>           Alter server parameters
  monitor <monitor> <key> <value>         Alter monitor parameters
  service <service> <key> <value>         Alter service parameters
  service-filters <service> [filters...]  Alter filters of a service
  logging <key> <value>                   Alter logging parameters
  maxscale <key> <value>                  Alter MaxScale parameters

```

### alter server

`Usage: alter server <server> <key> <value>`

To display the server parameters, execute `show server <server>`

### alter monitor

`Usage: alter monitor <monitor> <key> <value>`

To display the monitor parameters, execute `show monitor <monitor>`

### alter service

`Usage: alter service <service> <key> <value>`

To display the service parameters, execute `show service <service>`. The
following list of parameters can be altered at runtime:

[
    "user",
    "passwd",
    "enable_root_user",
    "max_connections",
    "connection_timeout",
    "auth_all_servers",
    "optimize_wildcard",
    "strip_db_esc",
    "localhost_match_wildcard_host",
    "max_slave_connections",
    "max_slave_replication_lag"
]

### alter service-filters

`Usage: alter service-filters <service> [filters...]`

The order of the filters given as the second parameter will also be the order in
which queries pass through the filter chain. If no filters are given, all
existing filters are removed from the service.

For example, the command `maxctrl alter service filters my-service A B C` will
set the filter chain for the service `my-service` so that A gets the query first
after which it is passed to B and finally to C. This behavior is the same as if
the `filters=A|B|C` parameter was defined for the service.

### alter logging

`Usage: alter logging <key> <value>`

To display the logging parameters, execute `show logging`

### alter maxscale

`Usage: alter maxscale <key> <value>`

To display the MaxScale parameters, execute `show maxscale`. The following list
of parameters can be altered at runtime:

[
    "auth_connect_timeout",
    "auth_read_timeout",
    "auth_write_timeout",
    "admin_auth",
    "admin_log_auth_failures",
    "passive"
]

## rotate

```
Usage: rotate <command>

Commands:
  logs  Rotate log files by closing and reopening the files

```

### rotate logs

`Usage: rotate logs`

This command is intended to be used with the `logrotate` command.

## call

```
Usage: call <command>

Commands:
  command <module> <command> [params...]  Call a module command

```

### call command

`Usage: call command <module> <command> [params...]`

To inspect the list of module commands, execute `list commands`

## cluster

```
Usage: cluster <command>

Commands:
  diff <target>  Show difference between host servers and <target>.
  sync <target>  Synchronize the cluster with target MaxScale server.

```

### cluster diff

`Usage: cluster diff <target>`

The list of host servers is controlled with the --hosts option. The target
server should not be in the host list. Value of <target> must be in HOST:PORT
format

### cluster sync

`Usage: cluster sync <target>`

This command will alter all MaxScale instances given in the --hosts option to
represent the <target> MaxScale. If the synchronization of a MaxScale instance
fails, it will be disabled by executing the `stop maxscale` command on that
instance. Synchronization can be attempted again if a previous attempt failed
due to a network failure or some other ephemeral error. Any other errors require
manual synchronization of the MaxScale configuration files and a restart of the
failed Maxscale.

## api

```
Usage: api <command>

Commands:
  get <resource> [path]  Get raw JSON

API options:
  --sum  Calculate sum of API result. Only works for arrays of numbers e.g. `api
         get --sum servers data[].attributes.statistics.connections`.
                                                      [boolean] [default: false]

```

### api get

`Usage: get <resource> [path]`

Perform a raw REST API call. The path definition uses JavaScript syntax to
extract values. For example, the following command extracts all server states as
an array of JSON values: maxctrl api get servers data[].attributes.state

## classify

```
Usage: classify <statement>

```

