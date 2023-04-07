# Read-Write Splitting with MariaDB MaxScale

The goal of this tutorial is to configure a system that appears to the client as a single
database. MariaDB MaxScale will split the statements such that write statements are sent
to the primary server and read statements are balanced across the replica servers.

## Setting up MariaDB MaxScale

This tutorial is a part of [MariaDB MaxScale Tutorial](MaxScale-Tutorial.md).
Please read it and follow the instructions. Return here once basic setup is complete.

## Configuring the service

After configuring the servers and the monitor, we create a read-write-splitter service
configuration. Create the following section in your configuration file. The section name
is also the name of the service and should be meaningful. For this tutorial, we use the
name *Splitter-Service*.

```
[Splitter-Service]
type=service
router=readwritesplit
servers=dbserv1, dbserv2, dbserv3
user=maxscale
password=maxscale_pw
```
*router* defines the routing module used. Here we use *readwritesplit* for
query-level read-write-splitting.

A service needs a list of servers where queries will be routed to. The server names must
match the names of server sections in the configuration file and not the hostnames or
addresses of the servers.

The *user* and *password* parameters define the credentials the service uses to populate
user authentication data. These users were created at the start of the
[MaxScale Tutorial](MaxScale-Tutorial.md).

For increased security, see [password encryption](Encrypting-Passwords.md).

## Configuring the Listener

To allow network connections to a service, a network ports must be associated with it.
This is done by creating a separate listener section in the configuration file. A service
may have multiple listeners but for this tutorial one is enough.

```
[Splitter-Listener]
type=listener
service=Splitter-Service
protocol=MariaDBClient
port=3306
```

The *service* parameter tells which service the listener connects to. For the
*Splitter-Listener* we set it to *Splitter-Service*.

A listener must define the protocol module it uses. This must be *MariaDBClient* for all
database listeners. *port* defines the network port to listen on.

The optional *address*-parameter defines the local address the listener should bind to.
This may be required when the host machine has multiple network interfaces. The
default behavior is to listen on all network interfaces (the IPv6 address `::`).

## Starting MariaDB MaxScale

For the last steps, please return to [MaxScale Tutorial](MaxScale-Tutorial.md).
