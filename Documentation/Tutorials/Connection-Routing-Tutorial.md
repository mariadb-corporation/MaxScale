# Connection Routing with MariaDB MaxScale

The goal of this tutorial is to configure a system that has two ports available, one for
write connections and another for read connections. The read connections are load-
balanced across replica servers.

## Setting up MariaDB MaxScale

This tutorial is a part of the [MariaDB MaxScale Tutorial](MaxScale-Tutorial.md).
Please read it and follow the instructions. Return here once basic setup is complete.

## Configuring services

We want two services and ports to which the client application can connect. One service
routes client connections to the primary server, the other load balances between replica
servers. To achieve this, we need to define two services in the configuration file.

Create the following two sections in your configuration file. The section names are the
names of the services and should be meaningful. For this tutorial, we use the names
*Write-Service* and *Read-Service*.

```
[Write-Service]
type=service
router=readconnroute
router_options=master
servers=dbserv1, dbserv2, dbserv3
user=maxscale
password=maxscale_pw

[Read-Service]
type=service
router=readconnroute
router_options=slave
servers=dbserv1, dbserv2, dbserv3
user=maxscale
password=maxscale_pw
```

*router* defines the routing module used. Here we use *readconnroute* for
connection-level routing.

A service needs a list of servers to route queries to. The server names must
match the names of server sections in the configuration file and not the hostnames or
addresses of the servers.

The *router_options*-parameter tells the *readconnroute*-module which servers it should
route a client connection to. For the write service we use the `master`-type and for the
read service the `slave`-type.

The *user* and *password* parameters define the credentials the service uses to populate
user authentication data. These users were created at the start of the
[MaxScale Tutorial](MaxScale-Tutorial.md).

For increased security, see [password encryption](Encrypting-Passwords.md).

## Configuring the Listener

To allow network connections to a service, a network ports must be associated with it.
This is done by creating a separate listener section in the configuration file. A service
may have multiple listeners but for this tutorial one per service is enough.

```
[Write-Listener]
type=listener
service=Write-Service
protocol=MariaDBClient
port=3306

[Read-Listener]
type=listener
service=Read-Service
protocol=MariaDBClient
port=3307
```

The *service* parameter tells which service the listener connects to. For the
*Write-Listener* we set it to *Write-Service* and for the *Read-Listener* we set
it to *Read-Service*.

A listener must define the protocol module it uses. This must be *MariaDBClient* for all
database listeners. *port* defines the network port to listen on.

The optional *address*-parameter defines the local address the listener should bind to.
This may be required when the host machine has multiple network interfaces. The
default behavior is to listen on all network interfaces (the IPv6 address `::`).

## Starting MariaDB MaxScale

For the last steps, please return to [MaxScale Tutorial](MaxScale-Tutorial.md).
