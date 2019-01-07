# RabbitMQ Filter

## Overview

This filter is designed to extract queries and transform them into a canonical
form e.g. `INSERT INTO database.table VALUES ("John Doe", "Downtown",100,50.0);`
turns into `INSERT INTO database.table VALUES ("?", "?",?,?);`. The filter
pushes these canonized queries and their replies into a RabbitMQ broker where
they can later be retrieved from. The retrieval can be done with a custom
application or the [RabbitMQ Consumer Client](RabbitMQ-Consumer-Client.md)
utility tool, which reads the messages from the broker and sends the contents of
those messages as SQL queries to a database.

## Configuration

The configuration block for the **mqfilter** requires the minimal filter options
in its section within the MaxScale configuration file. Although the filter will
start, it will use the default values which only work with a freshly installed
RabbitMQ server and use its default values. This setup is mostly intended for
testing the filter.

The following is an example of an mqfilter configuration used for actual logging
of queries to a RabbitMQ broker on a different host.

```
[RabbitMQ]
type=filter
module=mqfilter
hostname=192.168.122.100
port=4000
username=messageuser
password=msgpwd
exchange=msg-ex-1
key=MaxScale
logging_trigger=object,schema,source
logging_strict=false
logging_log_all=false
logging_object=my1
logging_schema=test
logging_source_user=maxtest

[RabbitMQ-Service]
type=service
router=readconnrouter
servers=server1
user=myuser
password=mypasswd
filters=RabbitMQ
```

### Filter Options

The mqfilter filter does not support any filter options.

### Filter Parameters

The RabbitMQ filter has parameters to control which queries are logged based on
either the attributes of the user or the query itself. These can be combined to
to only log queries targeting a certain table in a certain database from a
certain user from a certain network address.


 Option | Description | Accepted Values | Default |
--------|-------------|-----------------|-------------
 logging_trigger  |  Set the logging level  |  `all, source, schema, object`  |  `all`  |
 logging_strict  |  Sets whether to trigger when any of the parameters match or only if all parameters match  |  `true, false`  |  `false`  |
 logging_log_all  |  Log only SELECT, UPDATE, DELETE and INSERT or all possible queries  |  `true, false`  |  `true`  |
 logging_source_user  |  Comma-separated list of usernames to log  |     |     |
 logging_source_host  |  Comma-separated list of hostnames to log  |     |     |
 logging_schema  |  Comma-separated list of databases  |     |     |
 logging_object  |  Comma-separated list of database objects  |
 hostname  |  The server hostname where the messages are sent  |    |  `localhost`  |
 port  |  Port to send the messages to  |    |  `5672`  |
 username  |  Server login username  |    |  `guest`  |
 password  |  Server login password  |    |  `guest`  |
 vhost  |  The virtual host location on the server, where the messages are sent  |    |  `/`  |
 exchange  |  The name of the exchange  |    |  `default_exchange`  |
 exchange_type  |  The type of the exchange  |  `direct, fanout, topic, headers`  |  `direct`  |
 key  |  The routing key used when sending messages to the exchange  |    |  `key`  |
 queue  |  The queue that will be bound to the used exchange  |    |    |
 ssl_CA_cert  |  Path to the CA certificate in PEM format  |    |    |
 ssl_client_cert  |  Path to the client certificate in PEM format  |    |    |
 ssl_client_key  |  Path to the client public key in PEM format  |    |    |
