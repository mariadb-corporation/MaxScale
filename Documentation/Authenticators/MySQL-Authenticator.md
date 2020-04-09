# MySQL Authenticator

The *mysqlauth*-module implements the client and backend authentication for the
server plugin *mysql_ native_password*. This is the default authentication
plugin used by both MariaDB and MySQL.

## Authenticator options

The following settings may be given in the *authenticator_options* of the
listener.

### `cache_dir`

Deprecated and ignored.

### `inject_service_user`

Deprecated and ignored.

### `log_password_mismatch`

This parameter takes a boolean value and is disabled by default. When enabled,
password hashes are logged in the error messages when authentication fails due
to a password mismatch between the one stored in MaxScale and the one given by
the user. This feature should only be used to diagnose authentication issues in
MaxScale and should be done on a secure system as the logging of the password
hashes can be considered a security risk.
