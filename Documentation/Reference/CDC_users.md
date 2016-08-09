#Change Data Capture users

CDC is a the new MaxScale protocol that allows compatible clients to authenticate and
register for Change Data Capture events.
The new protocol must be use in
conjunction with AVRO router which currently converts MySQL binlog events into
AVRO records.


Clients connect to CDC listener and try authentication using credentials such as username and password.

Users could be created by adding an entry into the 'cdcusers' file that should be available in /$datadir/$service_name/

Example file location is:```/var/lib/maxscale/CDC_service/cdcusers```

**Note**: If no users are found in that file or if it doesn't exist, the only available user will be the ```service user```, if specified in service section:

###The Avro conversion service
```
[avro-service]
type=service
router=avrorouter
source=replication-service
## default service user
#user=cdc_user
#password=cdc_password
```

###How to create CDC users

```
bash$ cdc_users [-h] USER PASSWORD
```

The output of this command should be appended to ```/var/lib/maxscale/<service
name>/cdcusers```

```
bash$ cdc_users user1 pass1 >> /var/lib/maxscale/avro-service/cdcusers
```

**Note:** New users will be loaded into MaxScale at the first authentication attempt.

Users could be removed by deleting the related rows in 'cdcusers' file.

