# Read/Write Splitting with MariaDB Replication

## Environment & Solution Space

The object of this tutorial is to have a system that appears to the clients of
MariaDB MaxScale as if there was a single database behind MariaDB MaxScale.
MariaDB MaxScale will split the statements such that write statements will be
sent to the current master server in the replication cluster and read statements
will be balanced across the rest of the slave servers.

## Setting up MariaDB MaxScale

The first part of this tutorial is covered in
[MariaDB MaxScale Tutorial](MaxScale-Tutorial.md). Please read it and follow the
instructions for setting up MariaDB MaxScale with the type of cluster you want
to use.

Once you have MariaDB MaxScale installed and the database users created, the
configuration file for MariaDB MaxScale can be written.

## Creating Your MariaDB MaxScale Configuration

MariaDB MaxScale configuration is defined in the file `maxscale.cnf` located in
the directory `/etc`. If you have installed MaxScale in the default location the
file path should be `/etc/maxscale.cnf`. This file is not created as part of the
installation process and must be manually created. A template file, which may be
used as a basis for your configuration, exists within the `/usr/share/maxscale`
directory.

A global section, marked `maxscale`, is included within every MariaDB MaxScale
configuration file. The section is used to set the values of various
process-wide parameters, for example the number of worker threads.

```
[maxscale]
threads=4

```

The first step is to create a Read/Write Splitter service. Create a section in
your configuration file and set the type to service. The section header is the
name of the service and should be meaningful to the administrator. Names may
contain whitespace.

```
[Splitter Service]
type=service
```

The router module needed for this service is named `readwritesplit`. The service
must contain a list of backend server names. The server names are the headers of
server sections in the configuration file and not the physical hostnames or
addresses of the servers.

```
[Splitter Service]
type=service
router=readwritesplit
servers=dbserv1, dbserv2, dbserv3
```

The final step in the service section is to add the username and password that
will be used to populate the user data from the database cluster. There are two
options for representing the password: either plain text or encrypted passwords.
To use encrypted passwords, a set of keys for encryption/decryption must be
generated. To generate the keys use the `maxkeys` command and pass the name of
the secrets file containing the keys.

```
maxkeys /var/lib/maxscale/.secrets

```

Once the keys have been created, use the `maxpasswd` command to generate the
encrypted password.

```
maxpasswd plainpassword

96F99AA1315BDC3604B006F427DD9484

```

The username and password, either encrypted or in plain text, are stored in the
service section.

```
[Splitter Service]
type=service
router=readwritesplit
servers=dbserv1, dbserv2, dbserv3
user=maxscale
passwd=96F99AA1315BDC3604B006F427DD9484
```

This completes the service definition. To have the service accept network
connections, a listener must be associated with it. The listener is defined in
its own section. The type should be `listener` with an entry `service` defining
the name of the service the listener is listening for. A service may have
multiple listeners.

```
[Splitter Listener]
type=listener
service=Splitter Service
```

A listener must also define the protocol module it will use for the incoming
network protocol, currently this must be the `MariaDBClient` protocol for all
database listeners. The listener must also supply the network port to listen on.

```
[Splitter Listener]
type=listener
service=Splitter Service
protocol=MariaDBClient
port=3306
```

An address parameter may be given if the listener is required to bind to a
particular network address when using hosts with multiple network addresses. The
default behavior is to listen on all network interfaces.

## Configuring the Monitor and Servers

The next step is the configuration of the monitor and the servers that the
service uses. This is process described in the
[Configuring MariaDB Monitor](Configuring-MariaDB-Monitor.md)
document.

## Configuring the Administrative Interface

The final stage in the configuration is to add the service which used by the
`maxadmin` command to connect to MariaDB MaxScale for monitoring and
administration purposes. The example below shows a service section and a
listener section.

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

# Starting MariaDB MaxScale

Upon completion of the configuration MariaDB MaxScale is ready to be started.
This may either be done manually by running the `maxscale` command or via the
service interface.

```
% maxscale
```

or

```
% service maxscale start
```

Check the error log in /var/log/maxscale to see if any errors are detected in
the configuration file and to confirm MariaDB MaxScale has been started. Also
the maxadmin command may be used to confirm that MariaDB MaxScale is running and
the services, listeners etc have been correctly configured.

```
% maxadmin list services

Services.
--------------------------+----------------------+--------+---------------
Service Name              | Router Module        | #Users | Total Sessions
--------------------------+----------------------+--------+---------------
Splitter Service          | readwritesplit       |      1 |     1
CLI                       | cli                  |      2 |     2
--------------------------+----------------------+--------+---------------

% maxadmin list servers

Servers.
-------------------+-----------------+-------+-------------+--------------------
Server             | Address         | Port  | Connections | Status
-------------------+-----------------+-------+-------------+--------------------
dbserv1            | 192.168.2.1     |  3306 |           0 | Running, Slave
dbserv2            | 192.168.2.2     |  3306 |           0 | Running, Master
dbserv3            | 192.168.2.3     |  3306 |           0 | Running, Slave
-------------------+-----------------+-------+-------------+--------------------

% maxadmin list listeners

Listeners.
---------------------+--------------------+-----------------+-------+--------
Service Name         | Protocol Module    | Address         | Port  | State
---------------------+--------------------+-----------------+-------+--------
Splitter Service     | MariaDBClient      | *               |  3306 | Running
CLI                  | maxscaled          | localhost       |  6603 | Running
---------------------+--------------------+-----------------+-------+--------
```


MariaDB MaxScale is now ready to start accepting client connections and routing
them to the master or slaves within your cluster. Other configuration options,
that can alter the criteria used for routing, are available. These include
monitoring the replication lag within the cluster and routing only to slaves
that are within a predetermined delay from the current master or using weights
to obtain unequal balancing operations. These options may be found in the
MariaDB MaxScale Configuration Guide. More details on the use of maxadmin can be
found in the document
[MaxAdmin - The MariaDB MaxScale Administration & Monitoring Client Application](Administration-Tutorial.md).
