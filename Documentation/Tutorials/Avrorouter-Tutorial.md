# Avrorouter Tutorial

This tutorial is a short introduction to the [Avrorouter](../Routers/Avrorouter.md), how to set it up and
how it interacts with the binlogrouter.

The avrorouter can also be deployed directly on the master server which removes
the need to use the binlogrouter. This does require a lot more disk space on
the master server as both the binlogs and the Avro format files are stored there.

The first part configures the services and sets them up for the binary log to Avro
file conversion. The second part of this tutorial uses the client listener
interface for the avrorouter and shows how to communicate with the the service
over the network.

![Binlog-Avro Translator](../Routers/images/Binlog-Avro.png)

# Configuration

We start by adding two new services into the configuration file. The first
service is the binlogrouter service which will read the binary logs from
the master server.

```
[replication-service]
type=service
router=binlogrouter
router_options=server-id=4000,
               master-id=3000,
               binlogdir=/home/markusjm/binlogs,
               mariadb10-compatibility=1,
               filestem=binlog
user=maxuser
passwd=maxpwd
```

The second service will read the binlogs as they are streamed from the master
and convert them into Avro format files.

```
# The Avro conversion service
[avro-service]
type=service
router=avrorouter
source=replication-service
```

You can see that the `source` parameter points to the service we defined before.
This service will be the data source for the avrorouter.

After the services have been defined, we add the listeners for the _replication-service_
and the _avro-service_.

```
# The listener for the Binlog Server
[replication-listener]
type=listener
service=replication-router
protocol=MySQLClient
port=4000

# The client listener for the Avro conversion service
[avro-listener]
type=listener
service=avro-service
protocol=CDC
port=4001
```

The _CDC_ protocol is a new protocol added with the avrorouter and, at the time
of writing, it is the only supported protocol for the avrorouter.

The _binlogdir_ is the location where the binary logs are stored and
where the avrorouter will read them. The _filestem_ is the name prefix of
the binary logs and this should be the same as the `log-bin` value in the master
server. These parameters should be the same for both services. The _avrodir_ is
where the converted Avro files are stored.

# Starting MariaDB MaxScale

The next step is to start MariaDB MaxScale and set up the binlogrouter. We do that by connecting
to the MySQL listener of the _replication_router_ service and executing a few commands.

```
CHANGE MASTER TO MASTER_HOST='172.18.0.1',
       MASTER_PORT=3000,
       MASTER_LOG_FILE='binlog.000001',
       MASTER_LOG_POS=4,
       MASTER_USER='maxuser',
       MASTER_PASSWORD='maxpwd';

START SLAVE;
```

This will start the replication of binary logs from the master server at
172.18.0.1:3000. For more details about the details of the commands, refer
to the [Binlogrouter](../Routers/Binlogrouter.md) documentation.

After the binary log streaming has started, the avrorouter will automatically
start converting the binlogs into Avro files. You can inspect the Avro files
by using the _maxavrocheck_ utility program.

```
[markusjm@localhost avrodata]$ ../bin/maxavrocheck test.t1.000001.avro
File sync marker: caaed7778bbe58e701eec1f96d7719a
/home/markusjm/build/avrodata/test.t1.000001.avro: 1 blocks, 1 records and 12 bytes
```
