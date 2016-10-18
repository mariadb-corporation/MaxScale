# MySQL Authenticator

The _MySQLAuth_ and _MySQLBackendAuth_ modules implement the client and
backend authentication for the MySQL native password authentication. This
is the default authentication plugin used by both MariaDB and MySQL.

These modules are the default authenticators for all MySQL connections and
needs no further configuration to work.

## Authenticator options

The client authentication module, _MySQLAuth_, supports authenticator options.

### `cache_dir`

The location where the user credential cache is stored. The default value
for this is `<cache dir>/<service name>/<listener name>/cache/` where
`<cache dir>` by default is `/var/cache`.

Each listener has its own user cache where the user credential information
queried from the backends is stored. This information is used to
authenticate users if a connection to the backend servers can't be made.

#### Example configuration

```
[Read-Write Listener]
type=listener
service=Read-Write Service
protocol=MySQLClient
port=4006
authenticator=MySQLAuth
authenticator_options=cache_dir=/tmp
```

### `inject_service_user`

Inject service credentials into the list of database users if loading of
users fails. This option takes a boolean value and it is enabled by
default.

When a connection to the backend database cannot be made, the service user
can be injected into the list of allowed users. This allows administrative
operations to be done via the SQL interface with modules that support it
e.g. the Binlogrouter and Maxinfo modules.

If users are loaded successfully, the service user credentials are _not_
injected into the list of users.

```
authenticator_options=inject_service_user=false
```
