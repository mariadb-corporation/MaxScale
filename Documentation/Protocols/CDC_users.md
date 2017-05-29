# Change Data Capture (CDC) users

Change Data Capture (CDC) is a new MaxScale protocol that allows compatible
clients to authenticate and register for Change Data Capture events.  The new
protocol must be use in conjunction with AVRO router which currently converts
MariaDB binlog events into AVRO records.  Clients connect to CDC listener and
authenticate using credentials provided in a format described in the [CDC Protocol documentation](CDC.md).

**Note**: If no users are found in that file or if it doesn't exist, the only
  available user will be the _service user_:

```
[avro-service]
type=service
router=avrorouter
source=replication-service
user=cdc_user
password=cdc_password
```

## Creating new CDC users

Starting with MaxScale 2.1, users can also be created through MaxAdmin:

```
maxadmin call command cdc add_user <service> <name> <password>
```

The _<name>_ should be the service name where the user is created. Older
versions of MaxScale should use the _cdc_users.py_ script.

```
bash$ cdc_users.py [-h] USER PASSWORD
```

The output of this command should be appended to the _cdcusers_ file at
`/var/lib/maxscale/<service name>/`.

```
bash$ cdc_users.py user1 pass1 >> /var/lib/maxscale/avro-service/cdcusers
```

Users can be deleted by removing the related rows in 'cdcusers' file. For
more details on the format of the _cdcusers_ file, read the [CDC Protocol documentation](CDC.md).
