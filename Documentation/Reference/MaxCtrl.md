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

# Options

All command accept the following global options.

```
  -u, --user      Username to use                    [string] [default: "admin"]
  -p, --password  Password for the user            [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format
                  and each value must be separated by spaces.
                                             [array] [default: "localhost:8989"]
  -s, --secure    Enable HTTPS requests             [boolean] [default: "false"]
  -t, --timeout   Request timeout in milliseconds    [number] [default: "10000"]
  -q, --quiet     Silence all output                [boolean] [default: "false"]
  --tsv           Print tab separated output        [boolean] [default: "false"]

Options:
  --version  Show version number                                       [boolean]
  --help     Show help                                                 [boolean]
```

### `list`

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
  users                List created network users
  commands             List module commands

```

### `show`

```
Usage: show <command>

Commands:
  server <server>    Show server
  service <service>  Show service
  monitor <monitor>  Show monitor
  session <session>  Show session
  filter <filter>    Show filter
  module <module>    Show loaded module
  maxscale           Show MaxScale information
  logging            Show MaxScale logging information
  commands <module>  Show module commands of a module

```

### `set`

```
Usage: set <command>

Commands:
  server <server> <state>  Set server state

```

### `clear`

```
Usage: clear <command>

Commands:
  server <server> <state>  Clear server state

```

### `enable`

```
Usage: enable <command>

Commands:
  log-priority <log>  Enable log priority [warning|notice|info|debug]
  account <name>      Activate a Linux user account for administrative use

Enable account options:
  --type  Type of user to create
                         [string] [choices: "admin", "basic"] [default: "basic"]

```

### `disable`

```
Usage: disable <command>

Commands:
  log-priority <log>  Disable log priority [warning|notice|info|debug]
  account <name>      Disable a Linux user account from administrative use

```

### `create`

```
Usage: create <command>

Commands:
  server <name> <host> <port>       Create a new server
  monitor <name> <module>           Create a new monitor
  listener <service> <name> <port>  Create a new listener
  user <name> <password>            Create a new network user

Common create options:
  --protocol               Protocol module name                         [string]
  --authenticator          Authenticator module name                    [string]
  --authenticator-options  Option string for the authenticator          [string]

Create server options:
  --services  Link the created server to these services                  [array]
  --monitors  Link the created server to these monitors                  [array]

Create monitor options:
  --servers           Link the created monitor to these servers          [array]
  --monitor-user      Username for the monitor user                     [string]
  --monitor-password  Password for the monitor user                     [string]

Create listener options:
  --interface              Interface to listen on       [string] [default: "::"]
  --tls-key                Path to TLS key                              [string]
  --tls-cert               Path to TLS certificate                      [string]
  --tls-ca-cert            Path to TLS CA certificate                   [string]
  --tls-version            TLS version to use                           [string]
  --tls-cert-verify-depth  TLS certificate verification depth           [string]

Create user options:
  --type  Type of user to create
                         [string] [choices: "admin", "basic"] [default: "basic"]

```

### `destroy`

```
Usage: destroy <command>

Commands:
  server <name>              Destroy an unused server
  monitor <name>             Destroy an unused monitor
  listener <service> <name>  Destroy an unused listener
  user <name>                Remove a network user

```

### `link`

```
Usage: link <command>

Commands:
  service <name> <server...>  Link servers to a service
  monitor <name> <server...>  Link servers to a monitor

```

### `unlink`

```
Usage: unlink <command>

Commands:
  service <name> <server...>  Unlink servers from a service
  monitor <name> <server...>  Unlink servers from a monitor

```

### `start`

```
Usage: start <command>

Commands:
  service <name>  Start a service
  monitor <name>  Start a monitor
  maxscale        Start MaxScale by starting all services

```

### `stop`

```
Usage: stop <command>

Commands:
  service <name>  Stop a service
  monitor <name>  Stop a monitor
  maxscale        Stop MaxScale by stopping all services

```

### `alter`

```
Usage: alter <command>

Commands:
  server <server> <key> <value>    Alter server parameters
  monitor <monitor> <key> <value>  Alter monitor parameters
  service <service> <key> <value>  Alter service parameters
  logging <key> <value>            Alter logging parameters
  maxscale <key> <value>           Alter MaxScale parameters

```

### `rotate`

```
Usage: rotate <command>

Commands:
  logs  Rotate log files by closing and reopening the files

```

### `call`

```
Usage: call <command>

Commands:
  command <module> <command>                Call a module command
  [parameters...]

```

### `cluster`

```
Usage: cluster <command>

Commands:
  diff <target>  Show difference between host servers and <target>. Value must
                 be in HOST:PORT format
  sync <target>  Synchronize the cluster with target MaxScale server.

```

