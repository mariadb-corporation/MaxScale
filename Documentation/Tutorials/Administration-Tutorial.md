# MariaDB MaxScale Administration Tutorial

The purpose of this tutorial is to introduce the MariaDB MaxScale Administrator
to a few of the common administration tasks. This is intended to be an
introduction for administrators who are new to MariaDB MaxScale and not a
reference to all the tasks that may be performed.

[TOC]

## Starting and Stopping MariaDB MaxScale

MaxScale uses systemd for managing the process. This means that normal
`systemctl` commands can be used to start and stop MaxScale. To start MaxScale,
use `systemctl start maxscale`. To stop it, use `systemctl stop maxscale`.

The systemd service file for MaxScale is located in
`/lib/systemd/system/maxscale.service`.

### Additional Options for MaxScale

Additional command line options and other systemd configuration options
can be given to MariaDB MaxScale by creating a drop-in file for the
service unit file. You can do this with the `systemctl edit maxscale.service`
command. For more information about systemd drop-in
files, refer to
[the systemctl man page](https://www.freedesktop.org/software/systemd/man/systemctl.html)
and
[the systemd documentation](https://www.freedesktop.org/software/systemd/man/systemd.unit.html).

## Checking The Status Of The MariaDB MaxScale Services

It is possible to use the maxctrl command to obtain statistics about the
services that are running within MaxScale. The maxctrl command `list services`
will give very basic information regarding services. This command may be either
run in interactive mode or passed on the maxctrl command line.

```
$ maxctrl list services
┌────────────────────────┬────────────────┬─────────────┬───────────────────┬────────────────────────────────────┐
│ Service                │ Router         │ Connections │ Total Connections │ Servers                            │
├────────────────────────┼────────────────┼─────────────┼───────────────────┼────────────────────────────────────┤
│ CLI                    │ cli            │ 1           │ 1                 │                                    │
├────────────────────────┼────────────────┼─────────────┼───────────────────┼────────────────────────────────────┤
│ RW-Split-Router        │ readwritesplit │ 1           │ 1                 │ server1, server2, server3, server4 │
├────────────────────────┼────────────────┼─────────────┼───────────────────┼────────────────────────────────────┤
│ RW-Split-Hint-Router   │ readwritesplit │ 1           │ 1                 │ server1, server2, server3, server4 │
├────────────────────────┼────────────────┼─────────────┼───────────────────┼────────────────────────────────────┤
│ SchemaRouter-Router    │ schemarouter   │ 1           │ 1                 │ server1, server2, server3, server4 │
├────────────────────────┼────────────────┼─────────────┼───────────────────┼────────────────────────────────────┤
│ Read-Connection-Router │ readconnroute  │ 1           │ 1                 │ server1                            │
└────────────────────────┴────────────────┴─────────────┴───────────────────┴────────────────────────────────────┘

```

Network listeners count as a user of the service, therefore there will always be
one user per network port in which the service listens. More details can be
obtained by using the "show service" command.

## What Clients Are Connected To MariaDB MaxScale

To determine what client are currently connected to MariaDB MaxScale, you can
use the `list sessions` command within maxctrl. This will give you IP address
and the ID of the session for that connection. As with any maxctrl
command this can be passed on the command line or typed interactively in
maxctrl.

```
$ maxctrl list sessions
┌────┬─────────┬──────────────────┬──────────────────────────┬──────┬─────────────────┐
│ Id │ User    │ Host             │ Connected                │ Idle │ Service         │
├────┼─────────┼──────────────────┼──────────────────────────┼──────┼─────────────────┤
│ 6  │ maxuser │ ::ffff:127.0.0.1 │ Thu Aug 27 10:39:16 2020 │ 4    │ RW-Split-Router │
└────┴─────────┴──────────────────┴──────────────────────────┴──────┴─────────────────┘
```

## Rotating the Log File

MariaDB MaxScale logs messages of different priority into a single log file.
With the exception if error messages that are always logged, whether messages of
a particular priority should be logged or not can be enabled via the maxctrl
interface or in the configuration file. By default, MaxScale keeps on writing to
the same log file. To prevent the file from growing indefinitely, the
administrator must take action.

The name of the log file is maxscale.log. When the log is rotated, MaxScale
closes the current log file and opens a new one using the same name.

Log file rotation is achieved by use of the `rotate logs` command
in maxctrl.

```
maxctrl rotate logs
```

As there currently is only the maxscale log, that is the only one that will be
rotated.

This may be integrated into the Linux _logrotate_ mechanism by adding a
configuration file to the /etc/logrotate.d directory. If we assume we want to
rotate the log files once per month and wish to keep 5 log files worth of
history, the configuration file would look as follows.

```
/var/log/maxscale/maxscale.log {
monthly
rotate 5
missingok
nocompress
sharedscripts
postrotate
\# run if maxscale is running
if test -n "`ps acx|grep maxscale`"; then
/usr/bin/maxctrl rotate logs
fi
endscript
}
```

MariaDB MaxScale will also rotate all of its log files if it receives the USR1
signal. Using this the logrotate configuration script can be rewritten as

```
/var/log/maxscale/maxscale.log {
monthly
rotate 5
missingok
nocompress
sharedscripts
postrotate
kill -USR1 `cat /var/run/maxscale/maxscale.pid`
endscript
}
```

In older versions MaxScale renamed the log file, behavior which is not fully
compliant with the assumptions of logrotate and may lead to issues, depending on
the used logrotate configuration file. From version 2.1 onward, MaxScale will
not itself rename the log file, but when the log is rotated, MaxScale will
simply close and reopen the same log file. That will make the behavior fully
compliant with logrotate.

## Taking Objects Temporarily Out of Use

### Putting Servers into Maintenance

MariaDB MaxScale supports the concept of maintenance mode for servers within a
cluster, this allows for planned, temporary removal of a database from the
cluster within the need to change the MariaDB MaxScale configuration.

```
maxctrl set server db-server-3 maintenance
```

To achieve the removal of a database server you can use the `set server` command
in maxctrl to set the maintenance mode flag for the server. This
may be done interactively within maxctrl or by passing the command on the
command line.

This will cause MariaDB MaxScale to stop routing any new requests to the server,
however if there are currently requests executing on the server these will not
be interrupted.

```
maxctrl clear server db-server-3 maintenance
```

Clearing the maintenance mode for a server will bring it back into use. If
multiple MariaDB MaxScale instances are configured to use the node then
maintenance mode must be set within each MariaDB MaxScale instance.

## Stopping and Starting Services

```
maxctrl stop service db-service
```

Services can be stopped to temporarily halt their use. Stopping a service will
cause it to stop accepting new connections until it is started. New connections
are not refused if the service is stopped and are queued instead. This means
that connecting clients will wait until the service is started again.

```
maxctrl start service db-service
```

Starting a service will cause it to accept all queued connections that were
created while it was stopped.

### Stopping and Starting Monitors

```
maxctrl stop monitor db-monitor
```

Stopping a monitor will cause it to stop monitoring the state of the servers
assigned to it. This is useful when the state of the servers is assigned
manually with `maxctrl set server`.

```
maxctrl start monitor db-monitor
```

Starting a monitor will make it resume monitoring of the servers. Any manually
assigned states will be overwritten by the monitor.

## Runtime Configuration Modification

The MaxScale configuration can be changed at runtime by using the `create`,
`alter` and `destroy` commands of `maxctrl`. These commands either create,
modify or destroy objects (servers, services, monitors etc.) inside
MaxScale. The exact syntax for each of the commands and any additional options
that they take can be seen with `maxctrl --help <command>`.

Not all parameters can be modified at runtime. Refer to the module documentation
for more information on which parameters can be modified at runtime. If a
parameter cannot be modified at runtime, the object can be destroyed and
recreated in order to change it.

All runtime changes are persisted in files stored by default in
`/var/lib/maxscale/maxscale.cnf.d/`. This means that any changes done at runtime
persist through restarts. Any changes done to objects in the main configuration
file are ignored if a persisted entry is found for it.

For example, if the address of a server is modified with `maxctrl alter server
db-server-1 address 192.168.0.100`, the file
`/var/lib/maxscale/maxscale.cnf.d/db-server-1.cnf` is created with the complete
configuration for the object. To remove all runtime changes for all objects,
remove all files found in `/var/lib/maxscale/maxscale.cnf.d`.

### Core Parameter Configuration

Modify global MaxScale parameters:

```
maxctrl alter maxscale auth_connect_timeout 5s
```

Some global parameters cannot be modified at runtime.  Refer to the
[Configuration Guide](../Getting-Started/Configuration-Guide.md) for a full list
of parameters that can be modified at runtime.

### Managing Servers

#### Create a new server

```
maxctrl create server db-server-1 192.168.0.100 3306
```

#### Modify a Server

```
maxctrl alter server db-server-1 port 3307
```

#### Destroy a Server

```
maxctrl destroy server db-server-1
```

A server can only be destroyed if it is not used by any services or monitors. To
automatically remove the server from the services and monitors that use it, use
the `--force` flag.

### Managing Monitors

#### Create a new Monitor

```
maxctrl create monitor db-monitor mariadbmon user=db-user password=db-password
```

#### Modify a Monitor

```
maxctrl alter monitor db-monitor monitor_interval 1000
```

#### Add Server to a Monitor

```
maxctrl link monitor db-monitor db-server-1
```

#### Remove a Server from a Monitor

```
maxctrl unlink monitor db-monitor db-server-1
```

#### Destroy a Monitor

```
maxctrl destroy monitor db-monitor
```

A monitor can only be destroyed if it is not monitoring any servers. To
automatically remove the servers from the monitor, use the `--force` flag.

### Managing Services

#### Create a New Service

```
maxctrl create service db-service readwritesplit user=db-user password=db-password
```

#### Modify a Service

```
maxctrl alter service db-service user new-db-user
```

#### Add Servers to a Service

```
maxctrl link service db-service db-server1
```

Any servers added to services will only be used by new sessions. Existing
sessions will use the servers that were available when they connected.

#### Remove Servers from a Service

```
maxctrl unlink service db-service db-server1
```

Similarly to adding servers, removing servers from a service will only affect
new sessions. Existing sessions keep using the servers even if they are removed
from a service.

#### Change the Filters of a Service

```
maxctrl alter service-filters my-regexfilter my-qlafilter
```

The order of the filters is significant: the first filter will be the first to
receive the query. The new set of filters will only be used by new
sessions. Existing sessions will keep using the filters that were configured
when they connected.

#### Destroy a Service

```
maxctrl destroy service db-service
```

The service can only be destroyed if it uses no servers or clusters and has no
listeners associated with it. To force destruction of a service even if it does
use servers or has listeners, use the `--force` flag. This will also destroy any
listeners associated with the service.

### Managing Filters

#### Create a New Filter

```
maxctrl create filter regexfilter match=ENGINE=MyISAM replace=ENGINE=InnoDB
```

#### Destroy a Filter

```
maxctrl destroy filter my-regexfilter
```

A filter can only be destroyed if it is not used by any services. To
automatically remove the filter from all services using it, use the `--force`
flag.

Filters cannot be altered at runtime in MaxScale 2.5. To modify the parameters
of a filter, destroy it and recreate it with the modified parameters.

### Managing Listeners

#### Create a New Listener

```
maxctrl create listener db-listener db-service 4006
```

#### Destroy a Listener

```
maxctrl destroy listener db-listener
```

Destroying a listener will close the network socket and stop it from accepting
new connections. Existing connections that were created through it will keep
displaying it as the originating listener.

Listeners cannot be moved from one service to another. In order to do this, the
listener must be destroyed and then recreated with the new service.

## Managing MaxCtrl and REST API Users

MaxCtrl uses the same credentials as the MaxScale REST API. These users can be
managed via MaxCtrl.

### Create a New MaxCtrl User

```
maxctrl create user basic-user basic-password
```

By default new users are only allowed to read data. To make the account an
administrative account, add the `--type=admin` option to the command:

```
maxctrl create user admin-user admin-password --type=admin
```

Administrative accounts are allowed to use all MaxCtrl commands and modify any
parts of MaxScale.


### Change the Password of an Existing User

```
maxctrl alter user admin-user new-admin-password
```

### Remove a User

```
maxctrl destroy user basic-user
```
