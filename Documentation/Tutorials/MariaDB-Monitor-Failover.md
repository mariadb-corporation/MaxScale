# Automatic Failover With MariaDB Monitor

The [MariaDB Monitor](../Monitors/MariaDB-Monitor.md) is not only capable
of monitoring the state of a MariaDB primary-replica cluster but is also
capable of performing _failover_ and _switchover_. In addition, in some
circumstances it is capable of _rejoining_ a primary that has gone down and
later reappears.

Note that the failover (and switchover and rejoin) functionality is only
supported in conjunction with GTID-based replication and initially only
for simple topologies, that is, 1 primary and several replicas.

The failover, switchover and rejoin functionality are inherent parts of
the _MariaDB Monitor_, but neither automatic failover nor automatic rejoin
are enabled by default.

The following examples have been written with the assumption that there
are four servers - `server1`, `server2`, `server3` and `server4` - of
which `server1` is the initial primary and the other servers are replicas.
In addition there is a monitor called _TheMonitor_ that monitors those
servers.

Somewhat simplified, the MaxScale configuration file would look like:
```
[server1]
type=server
address=192.168.121.51
port=3306
protocol=MariaDBBackend

[server2]
...

[server3]
...

[server4]
...

[TheMonitor]
type=monitor
module=mariadbmon
servers=server1,server2,server3,server4
...
```
# Manual Failover
If everything is in order, the state of the cluster will look something
like this:
```
$ maxctrl list servers
┌─────────┬─────────────────┬──────┬─────────────┬─────────────────┐
│ Server  │ Address         │ Port │ Connections │ State           │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server1 │ 192.168.121.51  │ 3306 │ 0           │ Master, Running │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server2 │ 192.168.121.190 │ 3306 │ 0           │ Slave, Running  │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server3 │ 192.168.121.112 │ 3306 │ 0           │ Slave, Running  │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server4 │ 192.168.121.201 │ 3306 │ 0           │ Slave, Running  │
└─────────┴─────────────────┴──────┴─────────────┴─────────────────┘
```
If the primary now for any reason goes down, then the cluster state will
look like this:
```
$ maxctrl list servers
┌─────────┬─────────────────┬──────┬─────────────┬────────────────┐
│ Server  │ Address         │ Port │ Connections │ State          │
├─────────┼─────────────────┼──────┼─────────────┼────────────────┤
│ server1 │ 192.168.121.51  │ 3306 │ 0           │ Down           │
├─────────┼─────────────────┼──────┼─────────────┼────────────────┤
│ server2 │ 192.168.121.190 │ 3306 │ 0           │ Slave, Running │
├─────────┼─────────────────┼──────┼─────────────┼────────────────┤
│ server3 │ 192.168.121.112 │ 3306 │ 0           │ Slave, Running │
├─────────┼─────────────────┼──────┼─────────────┼────────────────┤
│ server4 │ 192.168.121.201 │ 3306 │ 0           │ Slave, Running │
└─────────┴─────────────────┴──────┴─────────────┴────────────────┘
```
Note that the status for `server1` is _Down_.

Since failover is by default _not_ enabled, the failover mechanism must be
invoked manually:
```
$ maxctrl call command mariadbmon failover TheMonitor
OK
```
There are quite a few arguments, so let's look at each one separately
* `call command` indicates that it is a module command that is to be
   invoked,
* `mariadbmon` indicates the module whose command we want to invoke (that
is the MariaDB Monitor),
* `failover` is the command we want to invoke, and
* `TheMonitor` is the first and only argument to that command, the name of
the monitor as specified in the configuration file.

The MariaDB Monitor will now autonomously deduce which replica is the most
appropriate one to be promoted to primary, promote it to primary and modify
the other replicas accordingly.

If we now check the cluster state we will see that one of the remaining
replicas has been made into primary.

```
$ maxctrl list servers
┌─────────┬─────────────────┬──────┬─────────────┬─────────────────┐
│ Server  │ Address         │ Port │ Connections │ State           │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server1 │ 192.168.121.51  │ 3306 │ 0           │ Down            │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server2 │ 192.168.121.190 │ 3306 │ 0           │ Master, Running │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server3 │ 192.168.121.112 │ 3306 │ 0           │ Slave, Running  │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server4 │ 192.168.121.201 │ 3306 │ 0           │ Slave, Running  │
└─────────┴─────────────────┴──────┴─────────────┴─────────────────┘
```
If `server1` now reappears, it will not be rejoined to the cluster, as
shown by the following output:
```
$ maxctrl list servers
┌─────────┬─────────────────┬──────┬─────────────┬─────────────────┐
│ Server  │ Address         │ Port │ Connections │ State           │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server1 │ 192.168.121.51  │ 3306 │ 0           │ Running         │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server2 │ 192.168.121.190 │ 3306 │ 0           │ Master, Running │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server3 │ 192.168.121.112 │ 3306 │ 0           │ Slave, Running  │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server4 │ 192.168.121.201 │ 3306 │ 0           │ Slave, Running  │
└─────────┴─────────────────┴──────┴─────────────┴─────────────────┘
```
Had `auto_rejoin=true` been specified in the monitor section, then an
attempt to rejoin `server1` would have been made.

In MaxScale 2.2.1, rejoining cannot be initiated manually, but in a
subsequent version a command to that effect will be provided.

# Automatic Failover

To enable automatic failover, simply add `auto_failover=true` to the
monitor section in the configuration file.
```
[TheMonitor]
type=monitor
module=mariadbmon
servers=server1,server2,server3,server4
auto_failover=true
...
```
When everything is running fine, the cluster state looks like follows:
```
$ maxctrl list servers
┌─────────┬─────────────────┬──────┬─────────────┬─────────────────┐
│ Server  │ Address         │ Port │ Connections │ State           │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server1 │ 192.168.121.51  │ 3306 │ 0           │ Master, Running │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server2 │ 192.168.121.190 │ 3306 │ 0           │ Slave, Running  │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server3 │ 192.168.121.112 │ 3306 │ 0           │ Slave, Running  │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server4 │ 192.168.121.201 │ 3306 │ 0           │ Slave, Running  │
└─────────┴─────────────────┴──────┴─────────────┴─────────────────┘
```
If `server1` now goes down, failover will automatically be performed and
an existing replica promoted to new primary.
```
$ maxctrl list servers
┌─────────┬─────────────────┬──────┬─────────────┬────────────────────────┐
│ Server  │ Address         │ Port │ Connections │ State                  │
├─────────┼─────────────────┼──────┼─────────────┼────────────────────────┤
│ server1 │ 192.168.121.51  │ 3306 │ 0           │ Down                   │
├─────────┼─────────────────┼──────┼─────────────┼────────────────────────┤
│ server2 │ 192.168.121.190 │ 3306 │ 0           │ Master, Slave, Running │
├─────────┼─────────────────┼──────┼─────────────┼────────────────────────┤
│ server3 │ 192.168.121.112 │ 3306 │ 0           │ Slave, Running         │
├─────────┼─────────────────┼──────┼─────────────┼────────────────────────┤
│ server4 │ 192.168.121.201 │ 3306 │ 0           │ Slave, Running         │
└─────────┴─────────────────┴──────┴─────────────┴────────────────────────┘
```
If you are continuously monitoring the server states, you may notice for a
brief period that the state of `server1` is _Down_ and the state of
`server2` is still _Slave, Running_.

# Rejoin

To enable automatic rejoin, simply add `auto_rejoin=true` to the
monitor section in the configuration file.
```
[TheMonitor]
type=monitor
module=mariadbmon
servers=server1,server2,server3,server4
auto_rejoin=true
...
```

When automatic rejoin is enabled, the MariaDB Monitor will attempt to
rejoin a failed primary as a replica, if it reappears.

When everything is running fine, the cluster state looks like follows:
```
$ maxctrl list servers
┌─────────┬─────────────────┬──────┬─────────────┬─────────────────┐
│ Server  │ Address         │ Port │ Connections │ State           │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server1 │ 192.168.121.51  │ 3306 │ 0           │ Master, Running │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server2 │ 192.168.121.190 │ 3306 │ 0           │ Slave, Running  │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server3 │ 192.168.121.112 │ 3306 │ 0           │ Slave, Running  │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server4 │ 192.168.121.201 │ 3306 │ 0           │ Slave, Running  │
└─────────┴─────────────────┴──────┴─────────────┴─────────────────┘
```
Assuming `auto_failover=true` has been specified in the configuration
file, when `server1` goes down for some reason, failover will be performed
and we end up with the following cluster state:
```
$ maxctrl list servers
┌─────────┬─────────────────┬──────┬─────────────┬─────────────────┐
│ Server  │ Address         │ Port │ Connections │ State           │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server1 │ 192.168.121.51  │ 3306 │ 0           │ Down            │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server2 │ 192.168.121.190 │ 3306 │ 0           │ Master, Running │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server3 │ 192.168.121.112 │ 3306 │ 0           │ Slave, Running  │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server4 │ 192.168.121.201 │ 3306 │ 0           │ Slave, Running  │
└─────────┴─────────────────┴──────┴─────────────┴─────────────────┘
```
If `server1` now reappears, the MariaDB Monitor will detect that and
attempt to rejoin the old primary as a replica.

Whether rejoining will succeed depends upon the actual state of the old
primary. For instance, if the old primary was modified and the changes had
not been replicated to the new primary, before the old primary went down,
then automatic rejoin will not be possible.

If rejoining can be performed, then the cluster state will end up looking
like:
```
$ maxctrl list servers
┌─────────┬─────────────────┬──────┬─────────────┬─────────────────┐
│ Server  │ Address         │ Port │ Connections │ State           │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server1 │ 192.168.121.51  │ 3306 │ 0           │ Slave, Running  │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server2 │ 192.168.121.190 │ 3306 │ 0           │ Master, Running │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server3 │ 192.168.121.112 │ 3306 │ 0           │ Slave, Running  │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server4 │ 192.168.121.201 │ 3306 │ 0           │ Slave, Running  │
└─────────┴─────────────────┴──────┴─────────────┴─────────────────┘
```

# Switchover

Switchover is for cases when you explicitly want to move the primary
role from one server to another.

If we continue from the cluster state at the end of the previous example
and want to make `server1` primary again, then we must issue the following
command:
```
$ maxctrl call command mariadbmon switchover TheMonitor server1 server2
OK
```
There are quite a few arguments, so let's look at each one separately
* `call command` indicates that it is a module command that is to be
   invoked,
* `mariadbmon` indicates the module whose command we want to invoke,
* `switchover` is the command we want to invoke, and
* `TheMonitor` is the first argument to the command, the name of the monitor
as specified in the configuration file,
* `server1` is the second argument to the command, the name of the server we
want to make into _primary_, and
* `server2` is the third argument to the command, the name of the _current_
_primary_.

If the command executes successfully, we will end up with the following
cluster state:
```
$ maxctrl list servers
┌─────────┬─────────────────┬──────┬─────────────┬─────────────────┐
│ Server  │ Address         │ Port │ Connections │ State           │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server1 │ 192.168.121.51  │ 3306 │ 0           │ Master, Running │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server2 │ 192.168.121.190 │ 3306 │ 0           │ Slave, Running  │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server3 │ 192.168.121.112 │ 3306 │ 0           │ Slave, Running  │
├─────────┼─────────────────┼──────┼─────────────┼─────────────────┤
│ server4 │ 192.168.121.201 │ 3306 │ 0           │ Slave, Running  │
└─────────┴─────────────────┴──────┴─────────────┴─────────────────┘
```
