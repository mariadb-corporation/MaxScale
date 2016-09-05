# Change Data Capture (CDC) users

Change Data Capture (CDC) is a new MaxScale protocol that allows compatible
clients to authenticate and register for Change Data Capture events.  The new
protocol must be use in conjunction with AVRO router which currently converts
MySQL binlog events into AVRO records.  Clients connect to CDC listener and
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
