# MariaDB MaxScale Docker image

This Docker image runs MariaDB MaxScale. The MaxScale version used to build the
image is an unreleased development build so this should not be used for a
production system.

## Usage

MaxScale is a proxy so its configuration is dependent on the use case and there
is no generally valid default. The configuration file inside the image (shown in
[Default configuration](#default-configuration)) includes only the bare minimum
to start MaxScale and has no useful services. The simplest way to add more to
the configuration is to place another file (*my_config.cnf*) in a directory
(*/my_dir/*) and then mount the directory to */etc/maxscale.cnf.d/* inside the
container when starting it. MaxScale will add any files inside
*/etc/maxscale.cnf.d/* to its configuration.

```
docker run --network host --rm -v /my_dir:/etc/maxscale.cnf.d/ maxscale
```

In the examples, the Docker network mode is set to *host* so that the container
has full network access (`--network host`).

To replace the configuration file completely, start MaxScale with `-f <path_to_config>`.
Adding custom options removes all default options defined in the image. When
adding new flags to MaxScale, one should add back `-l stdout` to print log to
stdout.

```
docker run --network host --rm -v /my_dir:/container_dir maxscale -l stdout -f /container_dir/my_config.cnf
```

To save logs to */my_dir*, remove the `-l stdout` and set */container_dir* as
log directory with the option `-L /container_dir`.

```
docker run --network host --rm -v /my_dir:/container_dir maxscale -L /container_dir -f /container_dir/my_config.cnf
```

## Default configuration

```
# MaxScale documentation on GitHub:
# https://github.com/mariadb-corporation/MaxScale/blob/develop/Documentation/Documentation-Contents.md

# Complete list of configuration options:
# https://github.com/mariadb-corporation/MaxScale/blob/develop/Documentation/Getting-Started/Configuration-Guide.md

# Global parameters

[maxscale]
threads=2

# This service enables the use of the MaxAdmin interface
# MaxScale administration guide:
# https://github.com/mariadb-corporation/MaxScale/blob/develop/Documentation/Reference/MaxAdmin.md
[MaxAdmin-Service]
type=service
router=cli

[MaxAdmin-Listener]
type=listener
service=MaxAdmin-Service
protocol=maxscaled
socket=default
```

## Example configuration extension

```
# Server definitions
# Set the address of the server to the network address of a MySQL server.

[server1]
type=server
address=127.0.0.1
port=3306
protocol=MySQLBackend

# Monitor for the servers
# This will keep MaxScale aware of the state of the servers.
# MySQL Monitor documentation:
# https://github.com/mariadb-corporation/MaxScale/blob/develop/Documentation/Monitors/MySQL-Monitor.md

[MySQL-Monitor]
type=monitor
module=mysqlmon
servers=server1
user=myuser
passwd=mypwd
monitor_interval=1000

# Service definitions
# Service Definition for a read-only service and a read/write splitting service.

# ReadConnRoute documentation:
# https://github.com/mariadb-corporation/MaxScale/blob/develop/Documentation/Routers/ReadConnRoute.md

[Read-Only-Service]
type=service
router=readconnroute
servers=server1
user=myuser
passwd=mypwd
router_options=slave

# ReadWriteSplit documentation:
# https://github.com/mariadb-corporation/MaxScale/blob/develop/Documentation/Routers/ReadWriteSplit.md

[Read-Write-Service]
type=service
router=readwritesplit
servers=server1
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
