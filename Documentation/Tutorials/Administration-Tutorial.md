# MariaDB MaxScale Administration Tutorial

The purpose of this tutorial is to introduce the MariaDB MaxScale Administrator
to a few of the common administration tasks. This is intended to be an
introduction for administrators who are new to MariaDB MaxScale and not a
reference to all the tasks that may be performed.

## Starting and Stopping MariaDB MaxScale

### Systemd

Most modern operating systems support the Systemd interface.

**Starting MaxScale:**
```
systemctl start maxscale
```

**Stopping MaxScale:**
```
systemctl stop maxscale
```

The MaxScale service file is located in `/lib/systemd/system/maxscale.service`.

### SysV

Legacy platforms should use the service interface to start MaxScale.

**Starting MaxScale:**
```
service maxscale start
```

**Stopping MaxScale:**
```
service maxscale stop
```

Additional command line arguments can be passed to MariaDB MaxScale with a
configuration file placed at `/etc/sysconfig/maxscale` on RPM installations and
`/etc/default/maxscale` file on DEB installations. Set the arguments in a
variable called `MAXSCALE_OPTIONS` and remember to surround the arguments with
quotes. The file should only contain environment variable declarations.

```
MAXSCALE_OPTIONS="--logdir=/home/maxscale/logs --piddir=/tmp --syslog=no"
```

Note that this is only supported on legacy SysV systems.

## Stopping MariaDB MaxScale via MaxAdmin

In order to shutdown MariaDB MaxScale using the maxadmin command you may either
connect with maxadmin in interactive mode or pass the "shutdown maxscale"
command you wish to execute as an argument to maxadmin.

```
sudo maxadmin shutdown maxscale
```

## Checking The Status Of The MariaDB MaxScale Services

It is possible to use the maxadmin command to obtain statistics about the
services that are running within MaxScale. The maxadmin command "list services"
will give very basic information regarding services. This command may be either
run in interactive mode or passed on the maxadmin command line.

```
	$ maxadmin
	MaxScale> list services

	Services.

	--------------------------+----------------------+--------+---------------

	Service Name              | Router Module        | #Users | Total Sessions

	--------------------------+----------------------+--------+---------------

	RWSplitter                | readwritesplit       |      2 |     4

	Cassandra                 | readconncouter       |      1 |     1

	CLI                       | cli                  |      2 |     2

	--------------------------+----------------------+--------+---------------

	MaxScale>
```

Network listeners count as a user of the service, therefore there will always be
one user per network port in which the service listens. More details can be
obtained by using the "show service" command.

## Persistent Connections

When clients who are accessing a database system through MariaDB MaxScale make
frequent short connections, there may be a benefit in using persistent
connections. This feature is controlled by two configuration values that are
specified per server in the relevant server section of the configuration file.
The configuration options are `persistpoolmax` and `persistmaxtime`.

Normally, when a client connection is terminated, all the related back end
database connections are also terminated. If the `persistpoolmax` options is set
to a non-zero integer, then up to that number of connections will be kept in a
pool for that server. When a new connection is requested by the system to handle
a client session, then a connection from the pool will be used if possible.

The connection will only be taken from the pool if it has been there for no more
than `persistmaxtime` seconds. The connection will also be discarded if it has
been disconnectedby the back end server. Connections will be selected so that
they match the user name and protocol for the new request.

Starting with MaxScale 2.1, when a MySQL protocol connection is taken from the
pool, the backend protocol module resets the session state. This allows
persistent connections to be used with no functional limitations.

The session state is reset when the first outgoing network transmission is done.
This _lazy initialization_ of the persistent connections allows MaxScale to take
multiple new connections into use but only initialize the ones that it actually
needs.

**Please note** that in versions before 2.1 the persistent connections may give
a different environment when compared to a fresh connection. For example, if the
previous use of the connection issued a "USE mydatabase;" statement then this
setting will be carried over into the reuse of the same connection. For many
applications this will not be noticeable, since each request will assume that
nothing has been set and will issue fresh requests such as "USE" to establish
the desired configuration. In exceptional cases this feature could be a problem.

It is possible to have pools for as many servers as you wish, with configuration
values in each server section.

## What Clients Are Connected To MariaDB MaxScale

To determine what client are currently connected to MariaDB MaxScale, you can
use the "list clients" command within maxadmin. This will give you IP address
and the ID’s of the DCB and session for that connection. As with any maxadmin
command this can be passed on the command line or typed interactively in
maxadmin.

```
	$ maxadmin list clients

	Client Connections

	-----------------+------------------+----------------------+------------

	 Client          | DCB              | Service              | Session

	-----------------+------------------+----------------------+------------

	 127.0.0.1       |   0x7fe694013410 | CLI                  | 0x7fe69401ac10

	-----------------+------------------+----------------------+------------

	$
```

## Rotating the Log File

MariaDB MaxScale logs messages of different priority into a single log file.
With the exception if error messages that are always logged, whether messages of
a particular priority should be logged or not can be enabled via the maxadmin
interface or in the configuration file. By default, MaxScale keeps on writing to
the same log file. To prevent the file from growing indefinitely, the
administrator must take action.

The name of the log file is maxscale.log. When the log is rotated, MaxScale
closes the current log file and opens a new one using the same name.

Log file rotation is achieved by use of the "flush log" or “flush logs” command
in maxadmin.

```
maxadmin flush logs
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
/usr/bin/maxadmin flush logs
fi
endscript
}
```

**Note**:

If 'root' user is no longer available for maxadmin connection and for example
'user1' is one of the allowed users, the maxadmin command should be run as:

`su - user1 -c '/usr/bin/maxadmin flush logs'`

If listening socket is not the default one, /tmp/maxadmin.sock, use -S option.

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

In older versions MaxScale renamed the log file, behaviour which is not fully
compliant with the assumptions of logrotate and may lead to issues, depending on
the used logrotate configuration file. From version 2.1 onwards, MaxScale will
not itself rename the log file, but when the log is rotated, MaxScale will
simply close and reopen (and truncate) the same log file. That will make the
behaviour fully compliant with logrotate.

## Taking A Database Server Out Of Use

MariaDB MaxScale supports the concept of maintenance mode for servers within a
cluster, this allows for planned, temporary removal of a database from the
cluster within the need to change the MariaDB MaxScale configuration.

To achieve the removal of a database server you can use the set server command
in the maxadmin utility to set the maintenance mode flag for the server. This
may be done interactively within maxadmin or by passing the command on the
command line.

```
sudo maxadmin set server dbserver3 maintenance
```

This will cause MariaDB MaxScale to stop routing any new requests to the server,
however if there are currently requests executing on the server these will not
be interrupted.

To bring the server back into service use the "clear server" command to clear
the maintenance mode bit for that server.

```
sudo maxadmin clear server dbserver3 maintenance
```

If multiple MariaDB MaxScale instances are configured to use the node
them maintenance mode must be set within each MariaDB MaxScale instance.
