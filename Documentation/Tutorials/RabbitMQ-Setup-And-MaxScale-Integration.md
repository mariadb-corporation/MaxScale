# Rabbit MQ setup and MariaDB MaxScale Integration
## Introduction
A step by step guide helps installing a RabbitMQ server and testing it before
MariaDB MaxScale integration.

New plugin filter and a message consumer application need to be compiled and
linked with an external C library, RabbitMQ-c, that provides AMQP protocol integration.
Custom configuration, with TCP/IP and Queue parameters, is also detailed here.
The software install setup provides RPM and DEB packaging and traditional compilation steps.

## Step 1 - Get the RabbitMQ binaries

On Centos 6.5 using fedora / RHEL rpm get the rpm from
[http://www.rabbitmq.com/](http://www.rabbitmq.com/ "RabbitMQ")

rabbitmq-server-3.3.4-1.noarch.rpm

Please note, before installing RabbitMQ, you must install Erlang.

Example:
```
yum install erlang
Package erlang-R14B-04.3.el6.x86_64 already installed and latest version
```

## Step 2 - Install and Start the Server

Install the packages using your distribution's package manager and start the server:

```
yum install rabbitmq-server-3.3.4-1.noarch.rpm
systemctl start rabbitmq-server.service

```
To configure your RabbitMQ server, please refer to the RabbitMQ website:
[http://www.rabbitmq.com/](http://www.rabbitmq.com/ RabbitMQ website).

rabbitmqctl is a command line tool for managing a RabbitMQ broker.
It performs all actions by connecting to one of the broker's nodes.

```
rabbitmqctl list_queues
rabbitmqctl list_queues | list_exchanges| cluster_status | list_bindings | list_connections | list_consumers | status
```

Example output:

```
            [root@maxscale-02 MaxScale]# rabbitmqctl status
            Status of node 'rabbit@maxscale-02' ...
            [{pid,12251},
             {running_applications,[{rabbit,"RabbitMQ","3.3.4"},
                        {os_mon,"CPO  CXC 138 46","2.2.7"},
                        {xmerl,"XML parser","1.2.10"},
                        {mnesia,"MNESIA  CXC 138 12","4.5"},
                        {sasl,"SASL  CXC 138 11","2.1.10"},
                        {stdlib,"ERTS  CXC 138 10","1.17.5"},
                        {kernel,"ERTS  CXC 138 10","2.14.5"}]},
                         {os,{unix,linux}},
                          {erlang_version,"Erlang R14B04 (erts-5.8.5) [source] [64-bit] [smp:2:2] [rq:2] [async-threads:30] [kernel-poll:true]\n"},
                          ...
                           {listeners,[{clustering,25672,"::"},{amqp,5672,"::"}]},
                           ...
                           ...done.


                           [root@maxscale-02 MaxScale]# rabbitmqctl list_bindings
                           Listing bindings ...
                           x1    exchange    q1    queue    k1    []
                           ...done.
```

Interaction with the server may require stop & reset at some point:

```
rabbitmqctl stop_app
rabbitmqctl reset
rabbitmqctl start_app
```

## Step 3 - Install and test the client libraries

The selected library for MariaDB MaxScale integration of RabbitMQ is:
[https://github.com/alanxz/rabbitmq-c](https://github.com/alanxz/rabbitmq-c RabbitMQ-C)

### Manual software compilation

To compile the RabbitMQ-C libraries manually:

```
git clone  https://github.com/alanxz/rabbitmq-c.git
cd rabbitmq-c
cmake -DCMAKE_INSTALL_PREFIX=/usr .
make
make install
```

Please note, this will install the packages to /usr. If you do not wish to install
them to this location, provide a different value for the CMAKE_INSTALL_PREFIX variable.


### Setup using the EPEL repository

Check how to configure your distribution for the EPEL repository:
[https://fedoraproject.org/wiki/EPEL](https://fedoraproject.org/wiki/EPEL EPEL)

Configure your repositories and install the software:

```
yum install librabbitmq.x86_64
```

you might also like to install:

```
librabbitmq-tools.x86_64, librabbitmq-devel.x86_64
```

Please note you may also install the rabbitmq server from the EPEL repository:

```
yum install rabbitmq-server
```

### Basic tests with library

The required library librabbitmq-c is now installed and we continue with basic
operations with amqp_* tools, located  in the examples/ folder of the build directory,
testing client server interaction.

Please note, those example applications may not be included in the RPM library packages.

#### Test 1 - create the exchange

```
[root@maxscale-02 examples]# ./amqp_exchange_declare
Usage: amqp_exchange_declare host port exchange exchangetype
```

Declare the exchange:

```
[root@maxscale-02 examples]# ./amqp_exchange_declare 127.0.0.1 5672 foo direct
```

#### Test 2 - Listen to exchange with selected binding key

```
[root@maxscale-02 examples]# ./amqp_listen
Usage: amqp_listen host port exchange bindingkey
```

Start the listener:

```
[root@maxscale-02 examples]# ./amqp_listen 127.0.0.1 5672 foo k1 &
```

#### Test 3 - Send a message …

```
[root@maxscale-02 examples]# ./amqp_sendstring
Usage: amqp_sendstring host port exchange routingkey messagebody

[root@maxscale-02 examples]# ./amqp_sendstring 127.0.0.1 5672 foo k1 “This is a new message”
```

... and watch the listener output

```
Delivery 1, exchange foo routingkey k1
Content-type: text/plain
```

## Step 4 - Configure new applications

The new filter needs to be configured in maxscale.cnf.

```
[Test-Service]
type=service
router=readconnroute
router_options=slave
servers=server1,server2,server3,server5,server4
user=massi
password=massi
filters=MQ

[MQ]
type=filter
module=mqfilter
exchange=x1
key=k1
queue=q1
hostname=127.0.0.1
port=5672
logging_trigger=all
```

Logging triggers define whether to log all or a subset of the incoming queries
using these options:

```
# log only some elements or all
logging_trigger=[all,source,schema,object]

# Whether to log only SELECT, UPDATE, INSERT and DELETE queries or all possible queries
logging_log_all=true|false

# Log only when any of the trigger parameters match or only if all parameters match
logging_strict=true|false

# specify objects
logging_object=mytable,another_table

# specify logged users
logging_source_user=testuser,testuser

# specify source addresses
logging_source_host=127.0.0.1,192.168.10.14

# specify schemas
logging_schema=employees,orders,catalog
```

Example:

```
logging_trigger=object,schema,source
logging_strict=false
logging_log_all=false
logging_object=my1
logging_schema=test
logging_source_user=maxtest
```

The logging result of the example is:

```
if user maxtest does something, it's logged
and all queries in test schema are logged
anything targeting my1 table is logged
SELECT NOW(), SELECT MD5(“xyz)” are not logged
```

Please note that if we want to log only the user ‘maxtest’ accessing the
schema ‘test’ with target ‘my1’  the option logging_strict must be set
to TRUE and if we want to include those selects without schema name the
option logging_log_all must be set to TRUE.

The mqfilter logs into the MariaDB MaxScale TRACE log information about
the matched logging triggers and the message delivering:

```
2014 09/03 06:22:04   Trigger is TRG_SOURCE: user: testuser = testuser
2014 09/03 06:22:04   Trigger is TRG_SCHEMA: test = test
2014 09/03 06:22:04   Trigger is TRG_OBJECT: test.t1 = t1
2014 09/03 06:22:04   Routing message to: 127.0.0.1:5672 / as guest/guest, exchange: x1<direct> key:k1 queue:q1

```

The consumer application needs to be configured as well:

```

#The options for the consumer are:
#hostname    RabbitMQ hostname
#port        RabbitMQ port
#vhost        RabbitMQ virtual host
#user        RabbitMQ username
#passwd    RabbitMQ password


#queue        Name of the queue to use
#dbserver    SQL server name
#dbport    SQL server port
#dbname    Name of the database to use
#dbuser    SQL server username
#dbpasswd    SQL server password
#logfile    Message log filename

[consumer]
hostname=127.0.0.1
port=5672
vhost=/
user=guest
passwd=guest
queue=q1
dbserver=127.0.0.1
dbport=3308
dbname=mqpairs
dbuser=xxx
dbpasswd=yyy
```

We may probably need to modify LD_LIBRARY_PATH before launching ‘consumer’:

```
# export LD_LIBRARY_PATH=/packages/rabbitmq-c/rabbitmq-c/librabbitmq:/packages/mariadb_client-2.0.0-Linux/lib/mariadb:/usr/lib64
```

and finally we can launch it:

```
# ./consumer
```

If the consumer.cnf file is not in the same directory as the binary file is,
you can provide the location of the folder that it is in by passing
it the -c flag followed by the path:

```
# ./consumer -c path/to/file
```

and start MariaDB MaxScale as well

## Step 5 - Test the filter and check collected data

Assuming that MariaDB MaxScale and the message consumer are successfully
running let’s connect to the service with an active mqfilter:

```
[root@maxscale-02 MaxScale]#  mysql -h 127.0.0.1 -P 4506 -uxxx -pyyy
...
MariaDB [(none)]> select RAND(3), RAND(5);
+--------------------+---------------------+
| RAND(3)            | RAND(5)             |
+--------------------+---------------------+
| 0.9057697559760601 | 0.40613597483014313 |
+--------------------+---------------------+
1 row in set (0.01 sec)
…
MariaDB [(none)]> select RAND(3544), RAND(11);
```

we can check the consumer output in the terminal where it was started:

```
--------------------------------------------------------------
Received: 1409671452|select @@version_comment limit ?
Received: 1409671452|Columns: 1
...
Received: 1409671477|select RAND(?), RAND(?)
Received: 1409671477|Columns: 2

We query now the database for the content collected so far:

MariaDB [(none)]> use mqpairs;
Database changed



MariaDB [mqpairs]> select * from pairs;

+-------------------------------------+----------------------------------+------------+---------------------+---------------------+---------+
| tag                                 | query                            | reply      | date_in             | date_out            | counter |
+-------------------------------------+----------------------------------+------------+---------------------+---------------------+---------+
| 006c006d006e006f007000710072007374  | select @@version_comment limit ? | Columns: 1 | 2014-09-02 11:14:51 | 2014-09-02 11:26:38 |       3 |
| 00750076007700780079007a007b007c7d  | SELECT DATABASE()                | Columns: 1 | 2014-09-02 11:14:56 | 2014-09-02 11:27:06 |       3 |
| 007e007f00800081008200830084008586  | show databases                   | Columns: 1 | 2014-09-02 11:14:56 | 2014-09-02 11:27:06 |       3 |
| 008700880089008a008b008c008d008e8f  | show tables                      | Columns: 1 | 2014-09-02 11:14:56 | 2014-09-02 11:27:06 |       3 |
| 0090009100920093009400950096009798  | select * from mqpairs.pairs      | Columns: 6 | 2014-09-02 11:15:00 | 2014-09-02 11:27:00 |      12 |
| 00fc00fd00fe00ff0100010101020103104 | select NOW()                     | Columns: 1 | 2014-09-02 11:24:23 | 2014-09-02 11:24:23 |       1 |
| 01050106010701080109010a010b010c10d | select RAND(?), RAND(?)          | Columns: 2 | 2014-09-02 11:24:37 | 2014-09-02 11:24:37 |       1 |
+-------------------------------------+----------------------------------+------------+---------------------+---------------------+---------+
7 rows in set (0.01 sec)
```

The filter send queries to the RabbitMQ server in the canonical format,
i.e select RAND(?), RAND(?).
The queries Message Queue Consumer application gets from the server
are stored with a counter that quickly shows how many times that normalized query was received:

```
| 01050106010701080109010a010b010c10d | select RAND(?), RAND(?)           | Columns: 2 | 2014-09-02 11:24:37 | 2014-09-02 11:29:15 |       3 |
```
