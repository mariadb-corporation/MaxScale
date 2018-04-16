# MariaDB MaxScale Docker image

This Docker image runs the latest GA version of MariaDB MaxScale.

## Building

Run the following command to build the image.

```
sudo docker build -t maxscale .
```

## Usage

You must mount your configuration file into `/etc/maxscale.cnf.d/`. To do
this, pass it as an argument to the `-v` option:

```
docker run -v $PWD/my-maxscale.cnf:/etc/maxscale.cnf.d/my-maxscale.cnf maxscale:latest
```

By default, MaxScale runs with the `-l stdout` arguments. To explicitly
define a configuration file, use the `-f /path/to/maxscale.cnf` argument
and add `-l stdout` after it.

```
docker run --network host --rm -v /my_dir:/container_dir maxscale -f /path/to/maxscale.cnf -l stdout
```

## Default configuration

```
# MaxScale documentation on GitHub:
# https://github.com/mariadb-corporation/MaxScale/blob/2.2/Documentation/Documentation-Contents.md

# Complete list of configuration options:
# https://github.com/mariadb-corporation/MaxScale/blob/2.2/Documentation/Getting-Started/Configuration-Guide.md

# Global parameters
[maxscale]
threads=auto

# This service enables the use of the MaxAdmin interface
# MaxScale administration guide:
# https://github.com/mariadb-corporation/MaxScale/blob/2.2/Documentation/Reference/MaxAdmin.md
[MaxAdmin-Service]
type=service
router=cli

[MaxAdmin-Listener]
type=listener
service=MaxAdmin-Service
protocol=maxscaled
socket=default
```

## Example base configuration

```
# Global parameters
[maxscale]
threads=auto

# Monitor for the servers
# This will keep MaxScale aware of the state of the servers.
# MySQL Monitor documentation:
# https://github.com/mariadb-corporation/MaxScale/blob/2.2/Documentation/Monitors/MariaDB-Monitor.md

[MariaDB-Monitor]
type=monitor
module=mariadbmon
user=myuser
passwd=mypwd
monitor_interval=1000

# Service definitions
# Service Definition for a read-only service and a read/write splitting service.

# ReadConnRoute documentation:
# https://github.com/mariadb-corporation/MaxScale/blob/2.2/Documentation/Routers/ReadConnRoute.md

[Read-Only-Service]
type=service
router=readconnroute
user=myuser
passwd=mypwd
router_options=slave

# ReadWriteSplit documentation:
# https://github.com/mariadb-corporation/MaxScale/blob/2.2/Documentation/Routers/ReadWriteSplit.md

[Read-Write-Service]
type=service
router=readwritesplit
user=myuser
passwd=mypwd
max_slave_connections=100%

# Listener definitions for the services
# Listeners represent the ports the services will listen on.

[Read-Only-Listener]
type=listener
service=Read-Only-Service
protocol=MySQLClient
port=4008

[Read-Write-Listener]
type=listener
service=Read-Write-Service
protocol=MySQLClient
port=4006
```

For base configurations, servers are defined at runtime. Run the following
command to create a server and link it into all services and monitors.

```
maxctrl create server <name> <host> <port> --monitors MariaDB-Monitor --services Read-Only-Service Read-Write-Service
```
