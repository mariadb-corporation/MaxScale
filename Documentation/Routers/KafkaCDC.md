# KafkaCDC

[TOC]

## Overview

The KafkaCDC module reads data changes in MariaDB via replication and converts
them into JSON objects that are then streamed to a Kafka broker.

DDL events (`CREATE TABLE`, `ALTER TABLE`) are streamed as JSON objects in the
following format (example created by `CREATE TABLE test.t1(id INT)`):

```javascript
{
  "namespace": "MaxScaleChangeDataSchema.avro",
  "type": "record",
  "name": "ChangeRecord",
  "table": "t2",              // name of the table
  "database": "test",         // the database the table is in
  "version": 1,               // schema version, incremented when the table format changes
  "gtid": "0-3000-14",        // GTID that created the current version of the table
  "fields": [
    {
      "name": "domain",       // First part of the GTID
      "type": "int"
    },
    {
      "name": "server_id",    // Second part of the GTID
      "type": "int"
    },
    {
      "name": "sequence",     // Third part of the GTID
      "type": "int"
    },
    {
      "name": "event_number", // Sequence number of the event inside the GTID
      "type": "int"
    },
    {
      "name": "timestamp",    // UNIX timestamp when the event was created
      "type": "int"
    },
    {
      "name": "event_type",   // Event type
      "type": {
        "type": "enum",
        "name": "EVENT_TYPES",
        "symbols": [
          "insert",           // The row that was inserted
          "update_before",    // The row before it was updated
          "update_after",     // The row after it was updated
          "delete"            // The row that was deleted
        ]
      }
    },
    {
      "name": "id",           // Field name
      "type": [
        "null",
        "long"
      ],
      "real_type": "int",     // Field type
      "length": -1,           // Field length, if found
      "unsigned": false       // Whether the field is unsigned
    }
  ]
}
```

The `domain`, `server_id` and `sequence` fields contain the GTID that this event
belongs to. The `event_number` field is the sequence number of events inside the
transaction starting from 1. The `timestamp` field is the UNIX timestamp when
the event occurred. The `event_type` field contains the type of the event, one
of:

* `insert`: the event is the data that was added to MariaDB
* `delete`: the event is the data that was removed from MariaDB
* `update_before`: the event contains the data before an update statement modified it
* `update_after`: the event contains the data after an update statement modified it

All remaining fields contains data from the table. In the example event this
would be the fields `id` and `data`.

DML events (`INSERT`, `UPDATE`, `DELETE`) are streamed as JSON objects that
follow the format specified in the DDL event. The objects are in the following
format (example created by `INSERT INTO test.t1 VALUES (1)`):

```javascript
{
  "domain": 0,
  "server_id": 3000,
  "sequence": 20,
  "event_number": 1,
  "timestamp": 1580485945,
  "event_type": "insert",
  "id": 1,
  "table_name": "t2",
  "table_schema": "test"
}
```

The `table_name` and `table_schema` fields were added in MaxScale 2.5.3. These
contain the table name and schema the event targets.

The router stores table metadata in the MaxScale data directory. The
default value is `/var/lib/maxscale/<service name>`. If data for a table
is replicated before a DDL event for it is replicated, the CREATE TABLE
will be queried from the master server.

During shutdown, the Kafka event queue is flushed. This can take up to 60
seconds if the network is slow or there are network problems.

## Configuration

The `servers` parameter defines the set of servers where the data is replicated
from. The replication will be done from the first master server that is found.

The `user` and `password` of the service will be used to connect to the
master. This user requires the REPLICATION SLAVE grant.

The KafkaCDC service must not be configured to use listeners. If a listener is
configured, all attempts to start a session will fail.

## Parameters

### `bootstrap_servers`

The list of Kafka brokers to use in `host:port` format. Multiple values
can be separated with commas. This is a mandatory parameter.

### `topic`

The Kafka topic where the replicated events will be sent. This is a
mandatory parameter.

### `enable_idempotence`

Enable idempotent producer mode. This feature requires Kafka version 0.11 or
newer to work and is disabled by default.

When enabled, the Kafka producer enters a strict mode which avoids event
duplication due to broker outages or other network errors. In HA scenarios where
there are more than two MaxScale instances, event duplication can still happen
as there is no synchronization between the MaxScale instances.

The Kafka C library,
[librdkafka](https://github.com/edenhill/librdkafka/blob/master/CONFIGURATION.md),
describes the parameter as follows:

> When set to true, the producer will ensure that messages are successfully
> produced exactly once and in the original produce order. The following
> configuration properties are adjusted automatically (if not modified by the
> user) when idempotence is enabled: max.in.flight.requests.per.connection=5 (must
> be less than or equal to 5), retries=INT32_MAX (must be greater than 0),
> acks=all, queuing.strategy=fifo.

### `timeout`

The connection and read timeout for the replication stream. The default
value is 10 seconds.

### `gtid`

The initial GTID position from where the replication is started. By default the
replication is started from the beginning. The value of this parameter is only
used if no previously replicated events with GTID positions can be retrieved
from Kafka.

#### `server_id`

The
[server_id](https://mariadb.com/kb/en/replication-and-binary-log-system-variables/#server_id)
used when replicating from the master in direct replication mode. The default
value is 1234. This parameter was added in MaxScale 6.0.

### `cooperative_replication`

Controls whether multiple instances cooperatively replicate from the same
cluster. This is a boolean parameter and is disabled by default. It was added in
MaxScale 6.0.

When this parameter is enabled and the monitor pointed to by the `cluster`
parameter supports cooperative monitoring (currently only `mariadbmon`), the
replication is only active if the monitor owns the cluster it is monitoring.

Whenever an instance that does not own the cluster gains ownership of the
cluster, the replication will continue from the latest GTID that was delivered
to Kafka.

This means that multiple MaxScale instances can replicate from the same set of
servers and the event is only processed once. This feature does not provide
exactly-once semantics for the Kafka event delivery. However, it does provide
high-availability for the `kafkacdc` instances which allows automated failover
between multiple MaxScale instances.

### `kafka_ssl`

Enable SSL for Kafka connections. This is a boolean parameter and is disabled by
default.

### `kafka_ssl_ca`

Path to the certificate authority file in PEM format. If this is not provided,
the default system certificates will be used.

### `kafka_ssl_cert`

Path to the public certificate in PEM format.

The client must provide a certificate if the Kafka server performs authentication
of the client certificates. This feature is enabled by default in Kafka and is
controlled by
[`ssl.endpoint.identification.algorithm`](https://kafka.apache.org/documentation/#brokerconfigs_ssl.endpoint.identification.algorithm).

If `kafka_ssl_cert` is provided, `kafka_ssl_key` must also be provided.

### `kafka_ssl_key`

Path to the private key in PEM format.

If `kafka_ssl_key` is provided, `kafka_ssl_cert` must also be provided.

### `kafka_sasl_user`

Username for SASL authentication.

If `kafka_sasl_user` is provided, `kafka_sasl_password` must also be provided.

### `kafka_sasl_password`

Password for SASL authentication.

If `kafka_sasl_password` is provided, `kafka_sasl_user` must also be provided.

### `kafka_sasl_mechanism`

The SASL mechanism used. The default value is `PLAIN` which uses plaintext
authentication. It is recommended to enable SSL whenever plaintext
authentication is used.

Allowed values are:

* `PLAIN`
* `SCRAM-SHA-256`
* `SCRAM-SHA-512`

The value that should be used depends on the SASL mechanism used by the
Kafka broker.

## Example Configuration

The following configuration defines the minimal setup for streaming replication
events from MariaDB into Kafka as JSON:

```
# The server we're replicating from
[server1]
type=server
address=127.0.0.1
port=3306
protocol=MariaDBBackend

# The monitor for the server
[MariaDB-Monitor]
type=monitor
module=mariadbmon
servers=server1
user=maxuser
password=maxpwd
monitor_interval=5000

# The MariaDB-to-Kafka CDC service
[Kafka-CDC]
type=service
router=kafkacdc
servers=server1
user=maxuser
password=maxpwd
bootstrap_servers=127.0.0.1:9092
topic=my-cdc-topic
```

## Limitations

* The KafkaCDC module provides at-least-once semantics for the generated
  events. This means that each replication event is delivered to kafka at least
  once but there can be duplicate events in case of failures.
