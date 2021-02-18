# Authentication Modules

This document describes general MySQL protocol authentication in MaxScale. For
REST-api authentication, see the
[configuration guide](../Getting-Started/Configuration-Guide.md) and the
[REST-api guide](../REST-API/API.md).

Similar to the MariaDB Server, MaxScale uses authentication plugins to implement
different authentication schemes for incoming clients. The same plugins also
handle authenticating the clients to backend servers. The authentication plugins
available in MaxScale are
[standard MySQL password](MySQL-Authenticator.md),
[GSSAPI](GSSAPI-Authenticator.md) and
[pluggable authentication modules (PAM)](PAM-Authenticator.md).

Most of the authentication processing is performed on the protocol level, before
handing it over to one of the plugins. This shared part is described in this
document. For information on an individual plugin, see its documentation.

## User account management

Every MaxScale service with a MariaDB protocol listener requires knowledge of
the user accounts defined on the backend databases. The service maintains this
information in an internal component called the *user account manager* (UAM).
The UAM queries relevant data from the *mysql*-database of the backends and
stores it. Typically, only the current master server is queried, as all servers
are assumed to have the same users. The service settings *user* and *password*
define the credentials used when fetching user accounts.

The service uses the stored data when authenticating clients, checking their
passwords and database access rights. This results in an authentication process
very similar to the MariaDB Server itself. Unauthorized users are generally
detected already at the MaxScale level instead of the backend servers. This may
not apply in some cases, for example if MaxScale is using old user account data.

If authentication fails, the UAM updates its data from a backend. MaxScale may
attempt authenticating the client again with the refreshed data without
communicating the first failure to the client. This transparent user data update
does not always work, in which case the client should try to log in again.

As the UAM is shared between all listeners of a service, its settings are
defined in the service configuration. For more information, search the
[configuration guide](../Getting-Started/Configuration-Guide.md)
for *users_refresh_time*, *users_refresh_interval* and
*auth_all_servers*. Other settings which affect how the UAM connects to backends
are the global settings *auth_connect_timeout* and *local_address*, and
the various server-level ssl-settings.

### Required grants

To properly fetch user account information, the MaxScale service user must be
able to read from various tables in the  *mysql*-database: *user*, *db*,
*tables_priv*, *columns_priv*, *procs_priv*, *proxies_priv* and *roles_mapping*.
The user should also have the *SHOW DATABASES*-grant.

```
CREATE USER 'maxscale'@'maxscalehost' IDENTIFIED BY 'maxscale-password';
GRANT SELECT ON mysql.user TO 'maxscale'@'maxscalehost';
GRANT SELECT ON mysql.db TO 'maxscale'@'maxscalehost';
GRANT SELECT ON mysql.tables_priv TO 'maxscale'@'maxscalehost';
GRANT SELECT ON mysql.columns_priv TO 'maxscale'@'maxscalehost';
GRANT SELECT ON mysql.procs_priv TO 'maxscale'@'maxscalehost';
GRANT SELECT ON mysql.proxies_priv TO 'maxscale'@'maxscalehost';
GRANT SELECT ON mysql.roles_mapping TO 'maxscale'@'maxscalehost';
GRANT SHOW DATABASES ON *.* TO 'maxscale'@'maxscalehost';
```

If using MariaDB ColumnStore, the following grant is required:

```
GRANT ALL ON infinidb_vtable.* TO 'maxscale'@'maxscalehost';
```

### Xpand

The system tables of Xpand are not the same as those of a regular
MariaDB server. Consquently, a different set of GRANTs are needed.

The service user must have the following grants:
```
CREATE USER 'maxscale'@'maxscalehost' IDENTIFIED BY 'maxscale-password';
GRANT SELECT ON system.users TO 'maxscale'@'maxscalehost';
GRANT SELECT ON system.user_acl TO 'maxscale'@'maxscalehost';
```

## Limitations and troubleshooting

When a client logs in to MaxScale, MaxScale sees the client's IP address. When
MaxScale then connects the client to backends (using the client's username and
password), the backends see the connection coming from the IP address of
MaxScale. If the client user account is to a wildcard host (`'alice'@'%'`), this
is not an issue. If the host is restricted (`'alice'@'123.123.123.123'`),
authentication to backends will fail.

There are two primary ways to deal with this:
1. Duplicate user accounts. For every user account with a restricted hostname an
equivalent user account for MaxScale is added (`'alice'@'maxscale-ip'`).
2. Use [proxy protocol](../Getting-Started/Configuration-Guide.md#proxy_protocol).

Option 1 limits the passwords for user accounts with shared usernames. Such
accounts must use the same password since they will effectively share the
MaxScale-to-backend user account. Option 2 requires server support.

See
[MaxScale Troubleshooting](https://mariadb.com/kb/en/mariadb-enterprise/maxscale-troubleshooting/)
for additional information on how to solve authentication issues.

### Wildcard database grants

MaxScale does not support wildcard grants to databases. Although on MariaDB
Server `grant select on test_.* to 'alice'@'%';` gives access to *test_* as well
as *test1*, *test2* ..., MaxScale only recognizes the grant to *test_*. If the
grant-command escapes the wildcard (``grant select on `test\_`.* to
'alice'@'%';``) both MaxScale and the MariaDB Server interpret it as only
allowing access to *test_*.

On the MaxScale side, this is performed by simply removing the escape character
`\` from the database name, controlled by the setting
[strip_db_esc](../Getting-Started/Configuration-Guide.mcd#strip_db_esc).

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
passwords of incoming clients and just assumes that they are correct.
Wrong passwords are instead detected when MaxScale tries to authenticate to the
backend servers.

This setting is mainly meant for failure tolerance in situations where the
password check is performed outside of MaxScale. If, for example, MaxScale
cannot use an LDAP-server but the backend databases can, enabling this setting
allows clients to log in. Even with this setting enabled, a user account
matching the incoming client username and IP must exist on the backends for
MaxScale to accept the client.

This setting is incompatible with standard MySQL authentication plugin
(*mysqlauth* in MaxScale). If enabled, MaxScale cannot authenticate clients to
backend servers using standard authentication.

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

Integer, default value is 0. Controls database name matching for authentication
when an incoming client logs in to a non-empty database. The setting functions
similar to the MariaDB Server setting
[lower_case_table_names](https://mariadb.com/kb/en/library/server-system-variables/#lower_case_table_names)
and should be set to the value used by the backends.

The setting accepts the values 0, 1 or 2:

0. Case-sensitive matching (default)
1. Convert the requested database name to lower case before using case-sensitive
matching. Assumes that database names on the server are stored in lower case.
2. Use case-insensitive matching.

*true* and *false* are also accepted for backwards compatibility. These map to 1
and 0, respectively.

The identifier names are converted using an ASCII-only function. This means that
non-ASCII characters will retain their case-sensitivity.

```
authenticator_options=lower_case_table_names=0
```
