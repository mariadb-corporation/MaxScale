# MongoDB Protocol

The MongoDB protocol module provides access, subject to a number
limitations, to MariaDB databases for unalterered MongoDB applications.

TBW

## Authentication

Currently no authentication is supported in the communication between
the MongoDB application and MaxScale. That is, the connection string
should look like
```
const uri = "mongodb://127.0.0.1:4711"
```
without a username and password.

The credentials to be used when MaxScale connects to the database on behalf
of the MongoDB application must be specified in the configuration as `user`
and `password` parameters to the MongoDB protocol module.
```
[MongoDB-Listener]
type=listener
protocol=mongodbclient
mongodbclient.user=the_username
mongodbclient.password=the_password
port=4711
...
```
Note that all MongoDB applications connecting to the MongoDB listener
port will use the same credentials when the MariaDB database is accessed;
in that respect it is not possible to distinguish one MongoDB application
from another.
