# MariaDB Protocol Module

The `mariadbprotocol` module implements the MariaDB client-server protocol.

The legacy protocol names `mysqlclient`, `mariadb` and `mariadbclient` are all
aliases to `mariadbprotocol`.

[TOC]

## Connection Redirection

The [Connection Redirection](https://mariadb.com/kb/en/connection-redirection-mechanism-in-the-mariadb-clientserver-protocol/)
introduced in MariaDB 11.4 allows client connections to be redirected to another
server if the server in question is going into maintenance. As MaxScale is
intended to be the gateway through which clients connect to the database
cluster, the use of `redirect_url` directly on the backend database servers
poses some challenges when used with MaxScale.

To prevent the accidental redirection of clients away from MaxScale, the
notification about the change of the system variable `redirect_url` is
intercepted by MaxScale and renamed into `mxs_rdir_url`. This prevents any
automated redirects from taking place while still allowing clients to see the
information if they need it.

To redirect clients away from MaxScale, use the
[redirect_url](../Getting-Started/Configuration-Guide.md#redirect_url)
parameter.

## Configuration

Protocol level parameters are defined in the listeners. They must be defined
using the scoped parameter syntax where the protocol name is used as the prefix.

```
[MyListener]
type=listener
service=MyService
protocol=mariadbprotocol
mariadbprotocol.allow_replication=false
port=3306
```

For the MariaDB protocol module, the prefix is always `mariadbprotocol`.

### `allow_replication`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: true

Whether the use of the replication protocol is allowed through this listener. If
disabled with `mariadbprotocol.allow_replication=false`, all attempts to start
replication will be rejected with a ER_FEATURE_DISABLED error (error number
1289).
