# MaxCtrl

MaxCtrl is a command line administrative client for MaxScale which uses
the MaxScale REST API for communication. It has replaced the legacy MaxAdmin
command line client that is no longer supported or included.

By default, the MaxScale REST API listens on port 8989 on the local host. The
default credentials for the REST API are `admin:mariadb`. The users used by the
REST API are the same that are used by the MaxAdmin network interface. This
means that any users created for the MaxAdmin network interface should work with
the MaxScale REST API and MaxCtrl.

For more information about the MaxScale REST API, refer to the
[REST API documentation](../REST-API/API.md) and the
[Configuration Guide](../Getting-Started/Configuration-Guide.md).

[TOC]

# .maxctrl.cnf

If the file `~/.maxctrl.cnf` exists, maxctrl will use any values in the
section `[maxctrl]` as defaults for command line arguments. For instance,
to avoid having to specify the user and password on the command line,
create the file `.maxctrl.cnf` in your home directory, with the following
content:
```
[maxctrl]
u = my-name
p = my-password
```
Note that all access rights to the file must be removed from everybody else
but the owner. MaxCtrl refuses to use the file unless the rights have been
removed.

Another file from which to read the defaults can be specified with the `-c`
flag.

# Commands

## list

### list servers

```
Usage: list servers

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

List all servers in MaxScale.


  Field       | Description
  -----       | -----------
  Server      | Server name
  Address     | Address where the server listens
  Port        | The port on which the server listens
  Connections | Current connection count
  State       | Server state
  GTID        | Current value of @@gtid_current_pos
```

### list services

```
Usage: list services

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

List all services and the servers they use.


  Field             | Description
  -----             | -----------
  Service           | Service name
  Router            | Router used by the service
  Connections       | Current connection count
  Total Connections | Total connection count
  Servers           | Servers that the service uses
```

### list listeners

```
Usage: list listeners [service]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

List listeners of all services. If a service is given, only listeners for that service are listed.


  Field   | Description
  -----   | -----------
  Name    | Listener name
  Port    | The port where the listener listens
  Host    | The address or socket where the listener listens
  State   | Listener state
  Service | Service that this listener points to
```

### list monitors

```
Usage: list monitors

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

List all monitors in MaxScale.


  Field   | Description
  -----   | -----------
  Monitor | Monitor name
  State   | Monitor state
  Servers | The servers that this monitor monitors
```

### list sessions

```
Usage: list sessions

Options:
      --rdns       Perform a reverse DNS lookup on client IPs  [boolean] [default: false]
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

List all client sessions.


  Field     | Description
  -----     | -----------
  Id        | Session ID
  User      | Username
  Host      | Client host address
  Connected | Time when the session started
  Idle      | How long the session has been idle, in seconds
  Service   | The service where the session connected
```

### list filters

```
Usage: list filters

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

List all filters in MaxScale.


  Field   | Description
  -----   | -----------
  Filter  | Filter name
  Service | Services that use the filter
  Module  | The module that the filter uses
```

### list modules

```
Usage: list modules

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

List all currently loaded modules.


  Field   | Description
  -----   | -----------
  Module  | Module name
  Type    | Module type
  Version | Module version
```

### list threads

```
Usage: list threads

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

List all worker threads.


  Field       | Description
  -----       | -----------
  Id          | Thread ID
  Current FDs | Current number of managed file descriptors
  Total FDs   | Total number of managed file descriptors
  Load (1s)   | Load percentage over the last second
  Load (1m)   | Load percentage over the last minute
  Load (1h)   | Load percentage over the last hour
```

### list users

```
Usage: list users

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

List network the users that can be used to connect to the MaxScale REST API.


  Field      | Description
  -----      | -----------
  Name       | User name
  Type       | User type
  Privileges | User privileges
```

### list commands

```
Usage: list commands

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

List all available module commands.


  Field    | Description
  -----    | -----------
  Module   | Module name
  Commands | Available commands
```

## show

### show server

```
Usage: show server <server>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Show detailed information about a server. The `Parameters` field contains the currently configured parameters for this server. See `--help alter server` for more details about altering server parameters.


  Field               | Description
  -----               | -----------
  Server              | Server name
  Address             | Address where the server listens
  Port                | The port on which the server listens
  State               | Server state
  Version             | Server version
  Last Event          | The type of the latest event
  Triggered At        | Time when the latest event was triggered at
  Services            | Services that use this server
  Monitors            | Monitors that monitor this server
  Master ID           | The server ID of the master
  Node ID             | The node ID of this server
  Slave Server IDs    | List of slave server IDs
  Current Connections | Current connection count
  Total Connections   | Total cumulative connection count
  Max Connections     | Maximum number of concurrent connections ever seen
  Statistics          | Server statistics
  Parameters          | Server parameters
```

### show servers

```
Usage: show servers

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Show detailed information about all servers.


  Field               | Description
  -----               | -----------
  Server              | Server name
  Address             | Address where the server listens
  Port                | The port on which the server listens
  State               | Server state
  Version             | Server version
  Last Event          | The type of the latest event
  Triggered At        | Time when the latest event was triggered at
  Services            | Services that use this server
  Monitors            | Monitors that monitor this server
  Master ID           | The server ID of the master
  Node ID             | The node ID of this server
  Slave Server IDs    | List of slave server IDs
  Current Connections | Current connection count
  Total Connections   | Total cumulative connection count
  Max Connections     | Maximum number of concurrent connections ever seen
  Statistics          | Server statistics
  Parameters          | Server parameters
```

### show service

```
Usage: show service <service>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Show detailed information about a service. The `Parameters` field contains the currently configured parameters for this service. See `--help alter service` for more details about altering service parameters.


  Field               | Description
  -----               | -----------
  Service             | Service name
  Router              | Router that the service uses
  State               | Service state
  Started At          | When the service was started
  Current Connections | Current connection count
  Total Connections   | Total connection count
  Max Connections     | Historical maximum connection count
  Cluster             | The cluster that the service uses
  Servers             | Servers that the service uses
  Services            | Services that the service uses
  Filters             | Filters that the service uses
  Parameters          | Service parameter
  Router Diagnostics  | Diagnostics provided by the router module
```

### show services

```
Usage: show services

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Show detailed information about all services.


  Field               | Description
  -----               | -----------
  Service             | Service name
  Router              | Router that the service uses
  State               | Service state
  Started At          | When the service was started
  Current Connections | Current connection count
  Total Connections   | Total connection count
  Max Connections     | Historical maximum connection count
  Cluster             | The cluster that the service uses
  Servers             | Servers that the service uses
  Services            | Services that the service uses
  Filters             | Filters that the service uses
  Parameters          | Service parameter
  Router Diagnostics  | Diagnostics provided by the router module
```

### show monitor

```
Usage: show monitor <monitor>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Show detailed information about a monitor. The `Parameters` field contains the currently configured parameters for this monitor. See `--help alter monitor` for more details about altering monitor parameters.


  Field               | Description
  -----               | -----------
  Monitor             | Monitor name
  Module              | Monitor module
  State               | Monitor state
  Servers             | The servers that this monitor monitors
  Parameters          | Monitor parameters
  Monitor Diagnostics | Diagnostics provided by the monitor module
```

### show monitors

```
Usage: show monitors

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Show detailed information about all monitors.


  Field               | Description
  -----               | -----------
  Monitor             | Monitor name
  Module              | Monitor module
  State               | Monitor state
  Servers             | The servers that this monitor monitors
  Parameters          | Monitor parameters
  Monitor Diagnostics | Diagnostics provided by the monitor module
```

### show session

```
Usage: show session <session>

Options:
      --rdns       Perform a reverse DNS lookup on client IPs  [boolean] [default: false]
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Show detailed information about a single session. The list of sessions can be retrieved with the `list sessions` command. The <session> is the session ID of a particular session.

The `Connections` field lists the servers to which the session is connected and the `Connection IDs` field lists the IDs for those connections.


  Field             | Description
  -----             | -----------
  Id                | Session ID
  Service           | The service where the session connected
  State             | Session state
  User              | Username
  Host              | Client host address
  Database          | Current default database of the connection
  Connected         | Time when the session started
  Idle              | How long the session has been idle, in seconds
  Parameters        | Session parameters
  Client TLS Cipher | Client TLS cipher
  Connections       | Ordered list of backend connections
  Connection IDs    | Thread IDs for the backend connections
  Queries           | Query history
  Log               | Per-session log messages
```

### show sessions

```
Usage: show sessions

Options:
      --rdns       Perform a reverse DNS lookup on client IPs  [boolean] [default: false]
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Show detailed information about all sessions. See `--help show session` for more details.


  Field             | Description
  -----             | -----------
  Id                | Session ID
  Service           | The service where the session connected
  State             | Session state
  User              | Username
  Host              | Client host address
  Database          | Current default database of the connection
  Connected         | Time when the session started
  Idle              | How long the session has been idle, in seconds
  Parameters        | Session parameters
  Client TLS Cipher | Client TLS cipher
  Connections       | Ordered list of backend connections
  Connection IDs    | Thread IDs for the backend connections
  Queries           | Query history
  Log               | Per-session log messages
```

### show filter

```
Usage: show filter <filter>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

The list of services that use this filter is show in the `Services` field.


  Field      | Description
  -----      | -----------
  Filter     | Filter name
  Module     | The module that the filter uses
  Services   | Services that use the filter
  Parameters | Filter parameters
```

### show filters

```
Usage: show filters

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Show detailed information of all filters.


  Field      | Description
  -----      | -----------
  Filter     | Filter name
  Module     | The module that the filter uses
  Services   | Services that use the filter
  Parameters | Filter parameters
```

### show listener

```
Usage: show listener <listener>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]




                                                           Field      | Description
                                                           -----      | -----------
                                                           Name       | Listener name
                                                           Service    | Services that the listener points to
                                                           Parameters | Listener parameters
```

### show listeners

```
Usage: show filters

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Show detailed information of all filters.


  Field      | Description
  -----      | -----------
  Filter     | Filter name
  Module     | The module that the filter uses
  Services   | Services that use the filter
  Parameters | Filter parameters
```

### show module

```
Usage: show module <module>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

This command shows all available parameters as well as detailed version information of a loaded module.


  Field       | Description
  -----       | -----------
  Module      | Module name
  Type        | Module type
  Version     | Module version
  Maturity    | Module maturity
  Description | Short description about the module
  Parameters  | All the parameters that the module accepts
  Commands    | Commands that the module provides
```

### show modules

```
Usage: show modules

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Displays detailed information about all modules.


  Field       | Description
  -----       | -----------
  Module      | Module name
  Type        | Module type
  Version     | Module version
  Maturity    | Module maturity
  Description | Short description about the module
  Parameters  | All the parameters that the module accepts
  Commands    | Commands that the module provides
```

### show maxscale

```
Usage: show maxscale

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

See `--help alter maxscale` for more details about altering MaxScale parameters.


  Field        | Description
  -----        | -----------
  Version      | MaxScale version
  Commit       | MaxScale commit ID
  Started At   | Time when MaxScale was started
  Activated At | Time when MaxScale left passive mode
  Uptime       | Time MaxScale has been running
  Config Sync  | MaxScale configuration synchronization
  Parameters   | Global MaxScale parameters
```

### show thread

```
Usage: show thread <thread>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Show detailed information about a worker thread.


  Field                  | Description
  -----                  | -----------
  Id                     | Thread ID
  Accepts                | Number of TCP accepts done by this thread
  Reads                  | Number of EPOLLIN events
  Writes                 | Number of EPOLLOUT events
  Hangups                | Number of EPOLLHUP and EPOLLRDUP events
  Errors                 | Number of EPOLLERR events
  Avg event queue length | Average number of events returned by one epoll_wait call
  Max event queue length | Maximum number of events returned by one epoll_wait call
  Max exec time          | The longest time spent processing events returned by a epoll_wait call
  Max queue time         | The longest time an event had to wait before it was processed
  Current FDs            | Current number of managed file descriptors
  Total FDs              | Total number of managed file descriptors
  Load (1s)              | Load percentage over the last second
  Load (1m)              | Load percentage over the last minute
  Load (1h)              | Load percentage over the last hour
  QC cache size          | Query classifier size
  QC cache inserts       | Number of times a new query was added into the query classification cache
  QC cache hits          | How many times a query classification was found in the query classification cache
  QC cache misses        | How many times a query classification was not found in the query classification cache
  QC cache evictions     | How many times a query classification result was evicted from the query classification cache
```

### show threads

```
Usage: show threads

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Show detailed information about all worker threads.


  Field                  | Description
  -----                  | -----------
  Id                     | Thread ID
  Accepts                | Number of TCP accepts done by this thread
  Reads                  | Number of EPOLLIN events
  Writes                 | Number of EPOLLOUT events
  Hangups                | Number of EPOLLHUP and EPOLLRDUP events
  Errors                 | Number of EPOLLERR events
  Avg event queue length | Average number of events returned by one epoll_wait call
  Max event queue length | Maximum number of events returned by one epoll_wait call
  Max exec time          | The longest time spent processing events returned by a epoll_wait call
  Max queue time         | The longest time an event had to wait before it was processed
  Current FDs            | Current number of managed file descriptors
  Total FDs              | Total number of managed file descriptors
  Load (1s)              | Load percentage over the last second
  Load (1m)              | Load percentage over the last minute
  Load (1h)              | Load percentage over the last hour
  QC cache size          | Query classifier size
  QC cache inserts       | Number of times a new query was added into the query classification cache
  QC cache hits          | How many times a query classification was found in the query classification cache
  QC cache misses        | How many times a query classification was not found in the query classification cache
  QC cache evictions     | How many times a query classification result was evicted from the query classification cache
```

### show logging

```
Usage: show logging

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

See `--help alter logging` for more details about altering logging parameters.


  Field              | Description
  -----              | -----------
  Current Log File   | The current log file MaxScale is logging into
  Enabled Log Levels | List of log levels enabled in MaxScale
  Parameters         | Logging parameters
```

### show commands

```
Usage: show commands <module>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

This command shows the parameters the command expects with the parameter descriptions.


  Field        | Description
  -----        | -----------
  Command      | Command name
  Parameters   | Parameters the command supports
  Descriptions | Parameter descriptions
```

### show qc_cache

```
Usage: show qc_cache

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Show contents (statement and hits) of query classifier cache.
```

### show dbusers

```
Usage: show dbusers <service>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Show information about the database users of the service
```

## set

### set server

```
Usage: set server <server> <state>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Set options:
      --force  Forcefully close all connections to the target server  [boolean] [default: false]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

If <server> is monitored by a monitor, this command should only be used to set the server into the `maintenance` state. Any other states will be overridden by the monitor on the next monitoring interval. To manually control server states, use the `stop monitor <name>` command to stop the monitor before setting the server states manually.
```

## clear

### clear server

```
Usage: clear server <server> <state>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

This command clears a server state set by the `set server <server> <state>` command
```

## drain

### drain server

```
Usage: drain server <server>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Drain options:
      --drain-timeout  Timeout for the drain operation in seconds. If exceeded, the server is added back to all services without putting it into maintenance mode.  [number] [default: 90]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

This command drains the server of connections by first removing it from all services after which it waits until all connections are closed. When all connections are closed, the server is put into the `maintenance` state and added back to all the services where it was removed from. To take the server back into use, execute `clear server <server> maintenance`.

Warning: This command is not safe to interrupt. If interrupted, the servers might not be added back to the service. For a better alternative, use `set server <server> drain`. This command has been deprecated in MaxScale 6.0.
```

## enable

### enable log-priority

```
Usage: enable log-priority <log>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

The `debug` log priority is only available for debug builds of MaxScale.
```

## disable

### disable log-priority

```
Usage: disable log-priority <log>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

The `debug` log priority is only available for debug builds of MaxScale.
```

## create

### create server

```
Usage: create server <name> <host|socket> [port]

Create server options:
      --services                     Link the created server to these services  [array]
      --monitors                     Link the created server to these monitors  [array]
      --protocol                     Protocol module name  [string] [default: "mariadbbackend"]
      --authenticator                Authenticator module name (deprecated)  [string]
      --authenticator-options        Option string for the authenticator (deprecated)  [string]
      --tls                          Enable TLS  [boolean]
      --tls-key                      Path to TLS key  [string]
      --tls-cert                     Path to TLS certificate  [string]
      --tls-ca-cert                  Path to TLS CA certificate  [string]
      --tls-version                  TLS version to use  [string]
      --tls-cert-verify-depth        TLS certificate verification depth  [number]
      --tls-verify-peer-certificate  Enable TLS peer certificate verification  [boolean]
      --tls-verify-peer-host         Enable TLS peer host verification  [boolean]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

The created server will not be used by any services or monitors unless the --services or --monitors options are given. The list of servers a service or a monitor uses can be altered with the `link` and `unlink` commands. If the <host|socket> argument is an absolute path, the server will use a local UNIX domain socket connection. In this case the [port] argument is ignored.
```

### create monitor

```
Usage: create monitor <name> <module> [params...]

Create monitor options:
      --servers           Link the created monitor to these servers. All non-option arguments after --servers are interpreted as server names e.g. `--servers srv1 srv2 srv3`.  [array]
      --monitor-user      Username for the monitor user  [string]
      --monitor-password  Password for the monitor user  [string]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

The list of servers given with the --servers option should not contain any servers that are already monitored by another monitor. The last argument to this command is a list of key=value parameters given as the monitor parameters.
```

### create service

```
Usage: service <name> <router> <params...>

Create service options:
      --servers   Link the created service to these servers. All non-option arguments after --servers are interpreted as server names e.g. `--servers srv1 srv2 srv3`.  [array]
      --filters   Link the created service to these filters. All non-option arguments after --filters are interpreted as filter names e.g. `--filters f1 f2 f3`.  [array]
      --services  Link the created service to these services. All non-option arguments after --services are interpreted as service names e.g. `--services svc1 svc2 svc3`.  [array]
      --cluster   Link the created service to this cluster (i.e. a monitor)  [string]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

The last argument to this command is a list of key=value parameters given as the service parameters. If the --servers, --services or --filters options are used, they must be defined after the service parameters. The --cluster option is mutually exclusive with the --servers and --services options.

Note that the `user` and `password` parameters must be defined.
```

### create filter

```
Usage: filter <name> <module> [params...]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

The last argument to this command is a list of key=value parameters given as the filter parameters.
```

### create listener

```
Usage: create listener <service> <name> <port>

Create listener options:
      --interface                    Interface to listen on  [string] [default: "::"]
      --protocol                     Protocol module name  [string] [default: "mariadbclient"]
      --authenticator                Authenticator module name  [string]
      --authenticator-options        Option string for the authenticator  [string]
      --tls-key                      Path to TLS key  [string]
      --tls-cert                     Path to TLS certificate  [string]
      --tls-ca-cert                  Path to TLS CA certificate  [string]
      --tls-version                  TLS version to use  [string]
      --tls-crl                      TLS CRL to use  [string]
      --tls-cert-verify-depth        TLS certificate verification depth  [number]
      --tls-verify-peer-certificate  Enable TLS peer certificate verification  [boolean]
      --tls-verify-peer-host         Enable TLS peer host verification  [boolean]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

The new listener will be taken into use immediately.
```

### create user

```
Usage: create user <name> <password>

Create user options:
      --type  Type of user to create  [string] [choices: "admin", "basic"] [default: "basic"]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

By default the created user will have read-only privileges. To make the user an administrative user, use the `--type=admin` option. Basic users can only perform `list` and `show` commands.
```

## destroy

### destroy server

```
Usage: destroy server <name>

Destroy options:
      --force  Remove the server from monitors and services before destroying it  [boolean] [default: false]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

The server must be unlinked from all services and monitor before it can be destroyed.
```

### destroy monitor

```
Usage: destroy monitor <name>

Destroy options:
      --force  Remove monitored servers from the monitor before destroying it  [boolean] [default: false]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

The monitor must be unlinked from all servers before it can be destroyed.
```

### destroy listener

```
Usage: destroy listener <service> <name>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Destroying a listener closes the listening socket, opening it up for reuse.
```

### destroy service

```
Usage: destroy service <name>

Destroy options:
      --force  Remove filters, listeners and servers from service before destroying it  [boolean] [default: false]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

The service must be unlinked from all servers and filters. All listeners for the service must be destroyed before the service itself can be destroyed.
```

### destroy filter

```
Usage: destroy filter <name>

Destroy options:
      --force  Automatically remove the filter from all services before destroying it  [boolean] [default: false]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

The filter must not be used by any service when it is destroyed.
```

### destroy user

```
Usage: destroy user <name>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

The last remaining administrative user cannot be removed. Create a replacement administrative user before attempting to remove the last administrative user.
```

## link

### link service

```
Usage: link service <name> <target...>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

This command links targets to a service, making them available for any connections that use the service. A target can be a server, another service or a cluster (i.e. a monitor). Before a server is linked to a service, it should be linked to a monitor so that the server state is up to date. Newly linked targets are only available to new connections, existing connections will use the old list of targets. If a monitor (a cluster of servers) is linked to a service, the service must not have any other targets linked to it.
```

### link monitor

```
Usage: link monitor <name> <server...>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Linking a server to a monitor will add it to the list of servers that are monitored by that monitor. A server can be monitored by only one monitor at a time.
```

## unlink

### unlink service

```
Usage: unlink service <name> <target...>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

This command unlinks targets from a service, removing them from the list of available targets for that service. New connections to the service will not use the unlinked targets but existing connections can still use the targets. A target can be a server, another service or a cluster (a monitor).
```

### unlink monitor

```
Usage: unlink monitor <name> <server...>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

This command unlinks servers from a monitor, removing them from the list of monitored servers. The servers will be left in their current state when they are unlinked from a monitor.
```

## start

### start service

```
Usage: start service <name>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

This starts a service stopped by `stop service <name>`
```

### start listener

```
Usage: start listener <name>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

This starts a listener stopped by `stop listener <name>`
```

### start monitor

```
Usage: start monitor <name>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

This starts a monitor stopped by `stop monitor <name>`
```

### start services

```
Usage: start [services|maxscale]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

This command will execute the `start service` command for all services in MaxScale.
```

## stop

### stop service

```
Usage: stop service <name>

Stop options:
      --force  Close existing connections after stopping the service  [boolean] [default: false]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Stopping a service will prevent all the listeners for that service from accepting new connections. Existing connections will still be handled normally until they are closed.
```

### stop listener

```
Usage: stop listener <name>

Stop options:
      --force  Close existing connections after stopping the listener  [boolean] [default: false]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Stopping a listener will prevent it from accepting new connections. Existing connections will still be handled normally until they are closed.
```

### stop monitor

```
Usage: stop monitor <name>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Stopping a monitor will pause the monitoring of the servers. This can be used to manually control server states with the `set server` command.
```

### stop services

```
Usage: stop [services|maxscale]

Stop options:
      --force  Close existing connections after stopping all services  [boolean] [default: false]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

This command will execute the `stop service` command for all services in MaxScale.
```

## alter

### alter server

```
Usage: alter server <server> <key> <value> ...

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

To display the server parameters, execute `show server <server>`.
```

### alter monitor

```
Usage: alter monitor <monitor> <key> <value> ...

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

To display the monitor parameters, execute `show monitor <monitor>`
```

### alter service

```
Usage: alter service <service> <key> <value> ...

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

To display the service parameters, execute `show service <service>`. Some routers support runtime configuration changes to all parameters. Currently all readconnroute, readwritesplit and schemarouter parameters can be changed at runtime. In addition to module specific parameters, the following list of common service parameters can be altered at runtime:

[
    "user",
    "passwd",
    "enable_root_user",
    "max_connections",
    "connection_timeout",
    "auth_all_servers",
    "optimize_wildcard",
    "strip_db_esc",
    "max_slave_connections",
    "max_slave_replication_lag",
    "retain_last_statements"
]
```

### alter service-filters

```
Usage: alter service-filters <service> [filters...]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

The order of the filters given as the second parameter will also be the order in which queries pass through the filter chain. If no filters are given, all existing filters are removed from the service.

For example, the command `maxctrl alter service filters my-service A B C` will set the filter chain for the service `my-service` so that A gets the query first after which it is passed to B and finally to C. This behavior is the same as if the `filters=A|B|C` parameter was defined for the service.
```

### alter filter

```
Usage: alter service <service> <key> <value> ...

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

To display the filter parameters, execute `show filter <filter>`. Some filters support runtime configuration changes to all parameters. Refer to the filter documentation for details on whether it supports runtime configuration changes and which parameters can be altered.
```

### alter listener

```
Usage: alter listener <listener> <key> <value> ...

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

To display the listener parameters, execute `show listener <listener>`
```

### alter logging

```
Usage: alter logging <key> <value> ...

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

To display the logging parameters, execute `show logging`
```

### alter maxscale

```
Usage: alter maxscale <key> <value> ...

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

To display the MaxScale parameters, execute `show maxscale`. The following list of parameters can be altered at runtime:

[
    "auth_connect_timeout",
    "auth_read_timeout",
    "auth_write_timeout",
    "admin_auth",
    "admin_log_auth_failures",
    "passive",
    "ms_timestamp",
    "skip_permission_checks",
    "query_retries",
    "query_retry_timeout",
    "retain_last_statements",
    "dump_last_statements"
]
```

### alter user

```
Usage: alter user <name> <password>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Changes the password for a user. To change the user type, destroy the user and then create it again.
```

### alter session

```
Usage: alter session <session> <key> <value> ...

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Alter parameters of a session. To get the list of modifiable parameters, use `show session <session>`
```

### alter session-filters

```
Usage: alter session-filters <session> [filters...]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

The order of the filters given as the second parameter will also be the order in which queries pass through the filter chain. If no filters are given, all existing filters are removed from the session. The syntax is similar to `alter service-filters`.
```

## rotate

### rotate logs

```
Usage: rotate logs

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

This command is intended to be used with the `logrotate` command.
```

## reload

### reload service

```
Usage: reload service <service>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]
```

## call

### call command

```
Usage: call command <module> <command> [params...]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

To inspect the list of module commands, execute `list commands`
```

## cluster

### cluster diff

```
Usage: cluster diff <target>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

The list of host servers is controlled with the --hosts option. The target server should not be in the host list. Value of <target> must be in HOST:PORT format
```

### cluster sync

```
Usage: cluster sync <target>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

This command will alter all MaxScale instances given in the --hosts option to represent the <target> MaxScale. Value of <target> must be in HOST:PORT format. Synchronization can be attempted again if a previous attempt failed due to a network failure or some other ephemeral error. Any other errors require manual synchronization of the MaxScale configuration files and a restart of the failed Maxscale.

Note: New objects created by `cluster sync` will have a placeholder value and must be manually updated. Passwords for existing objects will not be updated by `cluster sync` and must also be manually updated.
```

## api

### api get

```
Usage: get <resource> [path]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

API options:
      --sum     Calculate sum of API result. Only works for arrays of numbers e.g. `api get --sum servers data[].attributes.statistics.connections`.  [boolean] [default: false]
      --pretty  Pretty-print output.  [boolean] [default: false]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Perform a raw REST API call. The path definition uses JavaScript syntax to extract values. For example, the following command extracts all server states as an array of JSON values: maxctrl api get servers data[].attributes.state
```

### api post

```
Usage: post <resource> <value>

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

API options:
      --sum     Calculate sum of API result. Only works for arrays of numbers e.g. `api get --sum servers data[].attributes.statistics.connections`.  [boolean] [default: false]
      --pretty  Pretty-print output.  [boolean] [default: false]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Perform a raw REST API call. The provided value is passed as-is to the REST API after building it with JSON.parse
```

### api patch

```
Usage: patch <resource> [path]

Global Options:
  -c, --config    MaxCtrl configuration file  [string] [default: "~/.maxctrl.cnf"]
  -u, --user      Username to use  [string] [default: "admin"]
  -p, --password  Password for the user. To input the password manually, use -p '' or --password=''  [string] [default: "mariadb"]
  -h, --hosts     List of MaxScale hosts. The hosts must be in HOST:PORT format and each value must be separated by a comma.  [string] [default: "127.0.0.1:8989"]
  -t, --timeout   Request timeout in plain milliseconds, e.g '-t 1000', or as duration with suffix [h|m|s|ms], e.g. '-t 10s'  [string] [default: "10000"]
  -q, --quiet     Silence all output. Ignored while in interactive mode.  [boolean] [default: false]
      --tsv       Print tab separated output  [boolean] [default: false]

HTTPS/TLS Options:
  -s, --secure                  Enable HTTPS requests  [boolean] [default: false]
      --tls-key                 Path to TLS private key  [string]
      --tls-passphrase          Password for the TLS private key  [string]
      --tls-cert                Path to TLS public certificate  [string]
      --tls-ca-cert             Path to TLS CA certificate  [string]
  -n, --tls-verify-server-cert  Whether to verify server TLS certificates  [boolean] [default: true]

API options:
      --sum     Calculate sum of API result. Only works for arrays of numbers e.g. `api get --sum servers data[].attributes.statistics.connections`.  [boolean] [default: false]
      --pretty  Pretty-print output.  [boolean] [default: false]

Options:
      --version    Show version number  [boolean]
      --skip-sync  Disable configuration synchronization for this command  [boolean] [default: false]
      --help       Show help  [boolean]

Perform a raw REST API call. The provided value is passed as-is to the REST API after building it with JSON.parse
```

## classify

