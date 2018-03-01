# Connection Routing with Galera Cluster

# Environment & Solution Space

The object of this tutorial is to have a system that has two ports available, one for
write connections to the database cluster and the other for read connections to the database.

## Setting up MariaDB MaxScale

The first part of this tutorial is covered in [MariaDB MaxScale Tutorial](MaxScale-Tutorial.md).
Please read it and follow the instructions for setting up MariaDB MaxScale with the
type of cluster you want to use.

Once you have MariaDB MaxScale installed and the database users created, we can create
the configuration file for MariaDB MaxScale.

## Creating Your MariaDB MaxScale Configuration

MariaDB MaxScale configuration is held in an ini file that is located in the file maxscale.cnf
in the directory /etc, if you have installed in the default location then this file
is available in /etc/maxscale.cnf.  This is not created as part of the installation process
and must be manually created. A template file does exist within the /usr/share/maxscale directory
that may be use as a basis for your configuration.

A global, maxscale, section is included within every MariaDB MaxScale configuration file;
this is used to set the values of various MariaDB MaxScale wide parameters, perhaps
the most important of these is the number of threads that MariaDB MaxScale will use
to execute the code that forwards requests and handles responses for clients.

```
[maxscale]
threads=4
```

Since we are using Galera Cluster and connection routing we want a single to which
the client application can connect; MariaDB MaxScale will then route connections to
this port onwards to the various nodes within the Galera Cluster. To achieve this
within MariaDB MaxScale we need to define a service in the ini file. Create a section
for each in your MariaDB MaxScale configuration file and set the type to service,
the section name is the names of the service and should be meaningful to
the administrator. Names may contain whitespace.

```
[Galera Service]
type=service
```

The router for this section the readconnroute module, also the service should be
provided with the list of servers that will be part of the cluster. The server names
given here are actually the names of server sections in the configuration file and not
the physical hostnames or addresses of the servers.

```
[Galera Service]
type=service
router=readconnroute
servers=dbserv1, dbserv2, dbserv3
```

In order to instruct the router to which servers it should route we must add router
options to the service. The router options are compared to the status that the monitor
collects from the servers and used to restrict the eligible set of servers to which that
service may route. In our case we use the option that restricts us to servers that are
fully functional members of the Galera cluster which are able to support SQL operations
on the cluster. To achieve this we use the router option synced.

```
[Galera Service]
type=service
router=readconnroute
router_options=synced
servers=dbserv1, dbserv2, dbserv3
```

The final step in the service section is to add the username and password that will be
used to populate the user data from the database cluster. There are two options for
representing the password, either plain text or encrypted passwords may be used.
In order to use encrypted passwords a set of keys must be generated that will be used
by the encryption and decryption process. To generate the keys use the maxkeys command
and pass the name of the secrets file in which the keys are stored.

```
% maxkeys /var/lib/maxscale/.secrets
%
```

Once the keys have been created the maxpasswd command can be used to generate
the encrypted password.

```
% maxpasswd plainpassword
96F99AA1315BDC3604B006F427DD9484
%
```

The username and password, either encrypted or plain text, are stored in the service
section using the user and passwd parameters.

```
[Galera Service]
type=service
router=readconnroute
router_options=synced
servers=dbserv1, dbserv2, dbserv3
user=maxscale
passwd=96F99AA1315BDC3604B006F427DD9484
```

This completes the definitions required by the service, however listening ports must
be associated with a service in order to allow network connections. This is done by
creating a series of listener sections. These sections again are named for the convenience
of the administrator and should be of type listener with an entry labeled service which
contains the name of the service to associate the listener with.
Each service may have multiple listeners.

```
[Galera Listener]
type=listener
service=Galera Service
```

A listener must also define the protocol module it will use for the incoming
network protocol, currently this should be the MySQLClient protocol for all
database listeners. The listener may then supply a network port to listen on
and/or a socket within the file system.

```
[Galera Listener]
type=listener
service=Galera Service
protocol=MySQLClient
port=4306
socket=/tmp/DB.Cluster
```

An address parameter may be given if the listener is required to bind to a particular
network address when using hosts with multiple network addresses.
The default behavior is to listen on all network interfaces.

The next stage is the configuration is to define the server information.
This defines how to connect to each of the servers within the cluster, again a section
is created for each server, with the type set to server, the network address and port to
connect to and the protocol to use to connect to the server. Currently the protocol for
all database connections in MySQLBackend.

```
[dbserv1]
type=server
address=192.168.2.1
port=3306
protocol=MySQLBackend

[dbserv2]
type=server
address=192.168.2.2
port=3306
protocol=MySQLBackend

[dbserv3]
type=server
address=192.168.2.3
port=3306
protocol=MySQLBackend
```

In order for MariaDB MaxScale to monitor the servers using the correct monitoring
mechanisms a section should be provided that defines the monitor to use and
the servers to monitor. Once again a section is created with a symbolic name for
the monitor, with the type set to monitor. Parameters are added for the module to use,
the list of servers to monitor and the username and password to use when connecting to
the servers with the monitor.

```
[Galera Monitor]
type=monitor
module=galeramon
servers=dbserv1, dbserv2, dbserv3
user=maxscale
passwd=96F99AA1315BDC3604B006F427DD9484
```

As with the password definition in the server either plain text or encrypted
passwords may be used.

The final stage in the configuration is to add the option service which is used by
the maxadmin command to connect to MariaDB MaxScale for monitoring and
administration purposes. This creates a service section and a listener section.

```
[CLI]
type=service
router=cli
[CLI Listener]
type=listener
service=CLI
protocol=maxscaled
socket=default
```

## Starting MariaDB MaxScale

Upon completion of the configuration process MariaDB MaxScale is ready to be
started for the first time. This may either be done manually by running the
maxscale command or via the service interface.

```
% maxscale
```

or

```
% service maxscale start
```

Check the error log in /var/log/maxscale to see if any errors are detected in
the configuration file and to confirm MariaDB MaxScale has been started.
Also the maxadmin command may be used to confirm that MariaDB MaxScale is running
and the services, listeners etc have been correctly configured.

```
% maxadmin list services

Services.
--------------------------+----------------------+--------+---------------
Service Name              | Router Module        | #Users | Total Sessions
--------------------------+----------------------+--------+---------------
Galera Service            | readconnroute        |      1 |     1
CLI                       | cli                  |      2 |     2
--------------------------+----------------------+--------+---------------
% maxadmin list servers
Servers.
-------------------+-----------------+-------+-------------+-------------------
Server             | Address         | Port  | Connections | Status
-------------------+-----------------+-------+-------------+--------------------
dbserv1            | 192.168.2.1     |  3306 |           0 | Running, Synced, Master
dbserv2            | 192.168.2.2     |  3306 |           0 | Running, Synced, Slave
dbserv3            | 192.168.2.3     |  3306 |           0 | Running, Synced, Slave
-------------------+-----------------+-------+-------------+--------------------
```

A Galera Cluster is a multi-master clustering technology, however the monitor is able
to impose false notions of master and slave roles within a Galera Cluster in order to
facilitate the use of Galera as if it were a standard MariaDB Replication setup.
This is merely an internal MariaDB MaxScale convenience and has no impact on the behavior of the cluster.

You can control which Galera node is the master server by using the _priority_
mechanism of the Galera Monitor module. For more details,
read the [Galera Monitor](../Monitors/Galera-Monitor.md) documentation.

```
% maxadmin list listeners

Listeners.
---------------------+--------------------+-----------------+-------+--------
Service Name         | Protocol Module    | Address         | Port  | State
---------------------+--------------------+-----------------+-------+--------
Galera Service       | MySQLClient        | *               |  4306 | Running
CLI                  | maxscaled          | localhost       |  6603 | Running
---------------------+--------------------+-----------------+-------+--------
%
```

MariaDB MaxScale is now ready to start accepting client connections and routing
them to the master or slaves within your cluster. Other configuration options are
available that can alter the criteria used for routing, such as using weights to
obtain unequal balancing operations.
These options may be found in the MariaDB MaxScale Configuration Guide.
More detail on the use of maxadmin can be found in the document
["MaxAdmin - The MariaDB MaxScale Administration & Monitoring Client Application"](../Reference/MaxAdmin.md).
