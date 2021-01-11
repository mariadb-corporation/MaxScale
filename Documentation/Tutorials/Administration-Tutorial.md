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

## Taking A Database Server Out Of Use

MariaDB MaxScale supports the concept of maintenance mode for servers within a
cluster, this allows for planned, temporary removal of a database from the
cluster within the need to change the MariaDB MaxScale configuration.

To achieve the removal of a database server you can use the `set server` command
in maxctrl to set the maintenance mode flag for the server. This
may be done interactively within maxctrl or by passing the command on the
command line.

```
maxctrl set server dbserver3 maintenance
```

This will cause MariaDB MaxScale to stop routing any new requests to the server,
however if there are currently requests executing on the server these will not
be interrupted.

To bring the server back into service use the "clear server" command to clear
the maintenance mode bit for that server.

```
maxctrl clear server dbserver3 maintenance
```

If multiple MariaDB MaxScale instances are configured to use the node
them maintenance mode must be set within each MariaDB MaxScale instance.
