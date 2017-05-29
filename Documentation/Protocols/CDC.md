# Change Data Capture (CDC) Protocol

CDC is a new protocol that allows compatible clients to authenticate and
register for Change Data Capture events. The new protocol must be use in
conjunction with AVRO router which currently converts MariaDB binlog events into
AVRO records. Change Data Capture protocol is used by clients in order to
interact with stored AVRO file and also allows registered clients to be notified
with the new events coming from MariaDB 10.0/10.1 database.

## Creating Users

The users and their hashed passwords are stored in `/var/cache/maxscale/<service name>/cdcusers` where `<service name>` is the name of the service.

For example, the following service entry will look into `/var/cache/maxscale/CDC-Service/` for a file called `cdcusers`. If that file is found, the users in that file will be used for authentication.

```
[CDC-Service]
type=service
router=avrorouter
user=maxuser
passwd=maxpwd
```

If the `cdcusers` file cannot be found, the service user (_maxuser:maxpwd_ in the example) can be used to connect through the CDC protocol.

For more details, refer to the [CDC users documentation](CDC_users.md).

## Protocol Phases

### Connection and Authentication

- Client connects to MaxScale CDC protocol listener.
- Send the authentication message which includes the user and the SHA1 of the password

In the future, optional flags could be implemented.

### Registration

- Sending UUID
- Specify the output format (AVRO or JSON) for data retrieval.

### Data Request

- Send CDC commands to retrieve router statistics or to query for data events

## Protocol Details

### Authentication

The authentication starts when the client sends the hexadecimal representation
of the username concatenated with a colon (`:`) and the SHA1 of the password.

`bin2hex(username + ':' + SHA1(password))`

For example the user _foobar_ with a password of _foopasswd_ should send the
following hexadecimal string

```
foobar:SHA1(foopasswd) ->  666f6f6261723a3137336363643535253331
```

Server returns `OK` on success and `ERR` on failure.

### Registration

#### REGISTER

`REGISTER UUID=UUID, TYPE={JSON | AVRO}`

Register as a client to the service.

Example:

```
REGISTER UUID=11ec2300-2e23-11e6-8308-0002a5d5c51b, TYPE=AVRO
```

Server returns `OK` on success and `ERR` on failure.

### Change Data Capture Commands

#### REQUEST-DATA

`REQUEST-DATA DATABASE.TABLE[.VERSION] [GTID]`

This command fetches data from specified table in a database and returns the
output in the requested format (AVRO or JSON). Data records are sent to clients
and if new AVRO versions are found (e.g. _mydb.mytable.0000002.avro_) the new
schema and data will be sent as well.

The data will be streamed until the client closes the connection.

Clients should continue reading from network in order to automatically gets new events.

Example:

```
REQUEST-DATA db1.table1
REQUEST-DATA dbi1.table1.000003
REQUEST-DATA db2.table4 0-11-345
```

#### QUERY-LAST-TRANSACTION

`QUERY-LAST-TRANSACTION`

Returns JSON with last GTID, timestamp and affected tables.

Example output:

```
{"GTID": "0-1-178", "events": 2, "timestamp": 1462290084, "tables": ["db1.tb1", “db2.tb2”]}
```

Last GTID could then be used in a REQUEST-DATA query.

#### QUERY-TRANSACTION

`QUERY-TRANSACTION GTID`

Returns JSON from specified GTID, the commit timestamp and affected tables.

Example:

```
QUERY-TRANSACTION 0-14-1245
```

## Example Client

MaxScale includes an example CDC client application written in Python 3. You can
find the source code for it [in the MaxScale repository](https://github.com/mariadb-corporation/MaxScale/tree/2.0/server/modules/protocol/examples/cdc.py).
