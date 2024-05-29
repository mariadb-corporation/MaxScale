# MariaDB/MySQL Authenticator

The *MariaDBAuth*-module implements the client and backend authentication for the
server plugin *mysql_native_password*. This is the default authentication
plugin used by both MariaDB and MySQL.

## Authenticator options

The following settings may be given in the *authenticator_options* of the
listener.

### `clear_pw_passthrough`

Boolean, default value is "false". Activates passthrough-mode. In this mode,
MaxScale does not check client credentials at all and defers authentication to
the backend server. It may be useful in any situation where MaxScale
cannot check the existence of client user account nor authenticate the client.

When a client connects to a listener with this setting enabled, MaxScale will
change authentication method to "mysql_clear_password", causing the client to
send their cleartext password to MaxScale. MaxScale will then attempt to use
the password to authenticate to backends. The authentication result of the
first backend to respond will be sent to the client. The backend may ask
MaxScale for either cleartext password or standard ("mysql_native_password")
authentication token. MaxScale can work with both backend plugins since it has
the original password.

This feature is incompatible with service setting *lazy_connect*. Either leave
it unspecified or set `lazy_connect=false` in the linked service. Also,
multiple client authenticators are not allowed on the listener when
passthrough-mode is on.

Because passwords are sent in cleartext, the listener should be configured for
ssl.
```
[MyListener]
type=listener
authenticator=mariadbauth
authenticator_options=clear_pw_passthrough=true
ssl=true
<other options>
```

### `log_password_mismatch`

Boolean, default value is "false". The service setting *log_auth_warnings* must
also be enabled for this setting to have effect. When both settings are enabled,
password hashes are logged if a client gives a wrong password. This feature may
be useful when diagnosing authentication issues. It should only be enabled on a
secure system as the logging of password hashes may be a security risk.

### `cache_dir`

Deprecated and ignored.

### `inject_service_user`

Deprecated and ignored.
