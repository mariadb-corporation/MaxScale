# Authentication Modules

This document describes the general MySQL-protocol authentication in MaxScale.
For REST-api authentication, see the
[configuration guide](../Getting-Started/Configuration-Guide.md) and the
[REST-api guide](../REST-API/API.md).

Similar to the MariaDB-server, MaxScale uses authentication plugins to implement
different authentication schemes for incoming clients. The same plugins also
handle authenticating the clients to backend servers. The authentication plugins
available in MaxScale are
[standard MySQL password](MySQL-Authenticator.md),
[GSSAPI](GSSAPI-Authenticator.md) and
[pluggable authentication modules (PAM)](PAM-Authenticator.md).

Most of the authentication process is performed on the protocol level, before
handing it over to one of the plugins. This shared part is described in this
document. For information on an individual plugin, see its documentation.

## User account management

Every MaxScale service with a MySQL-protocol listener requires knowledge of the
user accounts defined on the backend databases. The service maintains this
information in a component called the *user account manager* (UAM). The UAM
queries relevant data from the *mysql.user*-database of the backends and stores
it. The service then uses the stored data to authenticate clients and check
their database access rights. This results in an authentication process very
similar to the MariaDB-server itself. Unauthorized users are generally detected
already at the MaxScale-level instead of the backend servers. This may not apply
in some cases, for example if MaxScale is using old user account data.

If authentication fails, the UAM updates its data from a backend. MaxScale may
attempt authenticating the client again with the refreshed data without
communicating the failure to client. This transparent user data update does not
always work, in which case the client should try to log in again.

As the UAM is shared between all listeners of a service, its settings are
defined in the service configuration. For more information, search the
[configuration guide](../Getting-Started/Configuration-Guide.md)
for *users_refresh_time*, *users_refresh_interval* and
*auth_all_servers*. The global settings *auth_connect_timeout* and
*local_address*, as well as server-level ssl-settings also affect user data
loading.

### Clustrix support

The UAM of the MaxScale MySQL-protocol implementation also supports Clustrix
servers. If the backends of the service are Clustrix servers, users are fetched
from the *system.users*-table and database grants from the
*system.user_acl*-table.

## Authenticator options

The listener configuration defines authentication options which only affect the
listener. *authenticator* defines the authentication plugins to use.
*authenticator_options* sets various options. These options may affect an
individual authentication plugin or the authentication as a whole. The latter
are explained below. Multiple options can be given as a comma-separated list.

```
authenticator_options=skip_authentication=true,lower_case_table_names=1
```

### `skip_authentication`

Boolean, default value is "false". If enabled, MaxScale will not check the
passwords of incoming clients and instead just assumes that they are correct.
Wrong passwords are instead detected when MaxScale tries to authenticate to the
backend servers.

This setting is mainly meant for failure tolerance in situations where the
password check is performed outside of MaxScale. If, for example, MaxScale
cannot use an LDAP-server but the backend databases can, enabling this setting
allows clients to log in. Even with this setting enabled, a user account
matching the incoming client must exist on the backends for MaxScale to accept
the client.

```
authenticator_options=skip_authentication=true
```

### `match_host`

Boolean, default value is "true". If disabled, MaxScale does not require that a
valid user account entry for incoming clients exists on the backends.
Specifically, only the client username needs to match a user account,
hostname/IP is ignored.

This setting may be used to force clients to connect through MaxScale. Normally,
creating the user *jdoe@%* will allow the user *jdoe* to connect from any
IP-address. By disabling *match_host* and replacing the user with
*jdoe@maxscale-IP*, the user can still connect from any client IP but will be
forced to go through MaxScale.

```
authenticator_options=match_host=false
```

### `lower_case_table_names`

Integer, default value is 0. Controls database name matching for authentication,
when an incoming client logs in to a non-empty database. The parameter functions
similar to the MariaDB server setting
[lower_case_table_names](https://mariadb.com/kb/en/library/server-system-variables/#lower_case_table_names)
and should be set to the value used by the server.

The parameter accepts the following values:
0. Case-sensitive matching (default)
1. Convert the requested database name to lower case before using case-sensitive
matching. Assumes that database names on the server are stored in lower case.
2. Use case-insensitive matching.

*true* and *false* are also accepted for backwards compatibility. These map to 1
and 0, respectively.

The identifier names are converted using an ASCII-only function. This means that
non-ASCII characters will retain their case-sensitivity.

```
authenticator_options=lower_case_table_names=false
```
