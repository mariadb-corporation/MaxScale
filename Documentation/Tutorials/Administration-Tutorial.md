# MaxScale Administration Tutorial

Last updated 24th June 2015

## Common Administration Tasks

The purpose of this tutorial is to introduce the MaxScale Administrator to a few of the common administration tasks that need to be performed with MaxScale. It is not intended as a reference to all the tasks that may be performed, more this is aimed as an introduction for administrators who are new to MaxScale.

[Starting MaxScale](#starting)   
[Stopping MaxScale](#stopping)   
[Checking The Status Of The MaxScale Services](#checking)  
[Persistent Connections](#persistent)  
[What Clients Are Connected To MaxScale](#clients)   
[Rotating Log Files](#rotating)   
[Taking A Database Server Out Of Use](#outofuse)   

<a name="starting"></a> 
### Starting MaxScale

There are several ways to start MaxScale, the most convenient mechanism is probably using the Linux service interface. When a MaxScale package is installed the package manager will also installed a script in /etc/init.d which may be used to start and stop MaxScale either directly or via the service interface.
```
	$ service maxscale start
```
or
```
	$ /etc/init.d/maxscale start
```
It is also possible to start MaxScale by executing the maxscale command itself. Running the executable /usr/bin/maxscale will result in MaxScale running as a daemon process, unattached to the terminal in which it was started and using configuration files that it finds in the /etc directory.

Options may be passed to the MaxScale binary that alter this default behavior, this options are documented in the table below.

Switch|Long Option|Description
------|-----------|-----------
`-d`|`--nodaemon`|enable running in terminal process (default:disabled)
`-f FILE`|`--config=FILE`|relative or absolute pathname of MaxScale configuration file (default:/etc/maxscale.cnf)
`-l[file shm]`|`--log=[file shm]`|log to file or shared memory (default: shm)
`-L PATH`|`--logdir=PATH`|path to log file directory (default: /var/log/maxscale)
`-D PATH`|`--datadir=PATH`|path to data directory, stored embedded mysql tables (default: /var/cache/maxscale)
`-C PATH`|`--configdir=PATH`|path to configuration file directory (default: /etc/)
`-B PATH`|`--libdir=PATH`|path to module directory (default: /usr/lib64/maxscale)
`-A PATH`|`--cachedir=PATH`|path to cache directory (default: /var/cache/maxscale)
`P PATH`|`--piddir=PATH`|PID file directory
`-U USER`|`--user=USER`|run MaxScale as another user. The user ID and group ID of this user are used to run MaxScale.
`-s [yes no]`|`--syslog=[yes no]`|log messages to syslog (default:yes)
`-S [yes no]`|`--maxscalelog=[yes no]`|log messages to MaxScale log (default: yes)
`-G [0 1]`|`--log_augmentation=[0 1]`|augment messages with the name of the function where the message was logged (default: 0). Primarily for development purposes.
`-v`|`--version`|print version info and exit
`-V`|`--version-full`|print version info and the commit ID the binary was built from
`-?`|`--help`|show this help

Additional command line arguments can be passed to MaxScale with a configuration file placed at `/etc/sysconfig/maxscale` on RPM installations and `/etc/default/maxscale` file on DEB installations. Set the arguments in a variable called `MAXSCALE_OPTIONS` and remember to surround the arguments with quotes. The file should only contain environment variable declarations.

```
MAXSCALE_OPTIONS="--logdir=/home/maxscale/logs --piddir=/tmp --syslog=no"
```

<a name="stopping"></a> 
### Stopping MaxScale

There are numerous ways in which MaxScale can be stopped; using the service interface, killing the process or by use of the maxadmin utility.

Stopping MaxScale with the service interface is simply a case of using the service stop command or calling the init.d script with the stop argument.
```
	$ service maxscale stop
```
or
```
	$ /etc/init.d/maxscale stop
```
MaxScale will also stop gracefully if it received a terminate signal, to find the process id of the MaxScale server use the ps command or read the contents of the maxscale.pid file located in the /var/run/maxscale directory.
```
	$ kill `cat /var/run/maxscale/maxscale.pid`
```
In order to shutdown MaxScale using the maxadmin command you may either connect with maxadmin in interactive mode or pass the "shutdown maxscale" command you wish 	to execute as an argument to maxadmin.
```
	$ maxadmin -pmariadb shutdown maxscale
```
<a name="checking"></a> 
### Checking The Status Of The MaxScale Services

It is possible to use the maxadmin command to obtain statistics regarding the services that are configured within your MaxScale configuration file. The maxadmin command "list services" will give very basic information regarding the services that are define. This command may be either run in interactive mode or passed on the maxadmin command line.

```
	$ maxadmin -pmariadb
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

It should be noted that network listeners count as a user of the service, therefore there will always be one user per network port in which the service listens. More detail can be obtained by use of the "show service" command which is passed a service name.

<a name="persistent"></a>
### Persistent Connections

Where the clients who are accessing a database system through MaxScale make frequent
short connections, there may be a benefit from invoking the MaxScale Persistent
Connection feature.  This is controlled by two configuration values that are specified
per server in the relevant server section of the configuration file.  The configuration
options are `persistpoolmax` and `persistmaxtime`.

Normally, when a client connection is terminated, all the related back end database
connections are also terminated.  If the `persistpoolmax` options is set to a non-zero
integer, then up to that number of connections will be kept in a pool for that 
server. When a new connection is requested by the system to meet a new client request, 
then a connection from the pool will be used if possible.

The connection will only be taken from the pool if it has been there for no more
than `persistmaxtime` seconds.  It was also be discarded if it has been disconnected
by the back end server. Connections will be selected that match the user name and
protocol for the new request.

Please note that because persistent connections have previously been in use, they
may give a different environment from a fresh connection. For example, if the 
previous use of the connection issued "use mydatabase" then this setting will be
carried over into the reuse of the same connection. For many applications this will
not be noticeable, since each request will assume that nothing has been set and
will issue fresh requests such as "use" to establish the desired configuration.  In 
exceptional cases this feature could be a problem.

It is possible to have pools for as many servers as you wish, with configuration
values in each server section.

<a name="clients"></a> 
### What Clients Are Connected To MaxScale

To determine what client are currently connected to MaxScale you can use the "list clients" command within maxadmin. This will give you IP address and the ID’s of the DCB and session for that connection. As with any maxadmin command this can be passed on the command line or typed interactively in maxadmin.
```
	$ maxadmin -pmariadb list clients

	Client Connections

	-----------------+------------------+----------------------+------------

	 Client          | DCB              | Service              | Session

	-----------------+------------------+----------------------+------------

	 127.0.0.1       |   0x7fe694013410 | CLI                  | 0x7fe69401ac10

	-----------------+------------------+----------------------+------------

	$
```
<a name="rotating"></a> 
### Rotating Log Files

MaxScale write log data into four log files with varying degrees of detail. With the exception of the error log, which can not be disabled, these log files may be enabled and disabled via the maxadmin interface or in the configuration file. The default behavior of MaxScale is to grow the log files indefinitely, the administrator must take action to prevent this.

It is possible to rotate either a single log file or all the log files with a single command. When the logfile is rotated, the current log file is closed and a new log file, with an increased sequence number in its name, is created.  Log file rotation is achieved by use of the "flush log" or “flush logs” command in maxadmin.
```
	$ maxadmin -pmariadb flush logs
```
Flushes all of the logs, whereas an individual log may be flushed with the "flush log" command.
```
	$ maxadmin -pmariadb
	MaxScale> flush log error
	MaxScale> flush log trace
	MaxScale>
```
This may be integrated into the Linux logrotate mechanism by adding a configuration file to the /etc/logrotate.d directory. If we assume we want to rotate the log files once per month and wish to keep 5 log files worth of history, the configuration file would look like the following.

```
/var/log/maxscale/*.log {
monthly
rotate 5
missingok
nocompress
sharedscripts
postrotate
\# run if maxscale is running
if test -n "`ps acx|grep maxscale`"; then
/usr/bin/maxadmin -pmariadb flush logs
fi
endscript
}
```

One disadvantage with this is that the password used for the maxadmin command has to be embedded in the log rotate configuration file. MaxScale will also rotate all of its log files if it receives the USR1 signal. Using this the logrotate configuration script can be rewritten as

```
/var/log/maxscale/*.log {
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

<a name="outofuse"></a> 
### Taking A Database Server Out Of Use

MaxScale supports the concept of maintenance mode for servers within a cluster, this allows for planned, temporary removal of a database from the cluster within the need to change the MaxScale configuration.

To achieve the removal of a database server you can use the set server command in the maxadmin utility to set the maintenance mode flag for the server. This may be done interactively within maxadmin or by passing the command on the command line.
```
	MaxScale> set server dbserver3 maintenance
	MaxScale>
```
This will cause MaxScale to stop routing any new requests to the server, however if there are currently requests executing on the server these will not be interrupted.

To bring the server back into service use the "clear server" command to clear the maintenance mode bit for that server.
```
	MaxScale> clear server dbserver3 maintenance
	MaxScale> 
```
Note that maintenance mode is not persistent, if MaxScale restarts when a node is in maintenance mode a new instance of MaxScale will not honor this mode. If multiple MaxScale instances are configured to use the node them maintenance mode must be set within each MaxScale instance. However if multiple services within one MaxScale instance are using the server then you only need set the maintenance mode once on the server for all services to take note of the mode change.

