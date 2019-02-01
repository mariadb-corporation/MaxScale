# PAM Authenticator

Pluggable authentication module (PAM) is a general purpose authentication API.
An application using PAM can authenticate a user without knowledge about the
underlying authentication implementation. The actual authentication scheme is
defined in the operating system PAM config (e.g. `/etc/pam.d/`), and can be
quite elaborate. MaxScale supports a very limited form of the PAM protocol,
which this document details.

## Configuration

The MaxScale PAM modules themselves have no configuration. All that is required
is to change the listener and backend authenticator modules to `PAMAuth` and
`PAMBackendAuth`, respectively.

```
[Read-Write-Listener]
type=listener
address=::
service=Read-Write-Service
protocol=MariaDBClient
authenticator=PAMAuth

[Master-Server]
type=server
address=123.456.789.10
port=12345
protocol=MariaDBBackend
authenticator=PAMBackendAuth
```

The PAM authenticator fetches user entries with `plugin='pam'` from
the `mysql.user` table of a backend. The user accounts also need to have either
the global SELECT-privilege or a database or a table-level privilege. The PAM
service name of a user is read from the `authetication_string`-column. The
matching PAM service in the operating system PAM config is used for
authenticating the user. If the `authetication_string` for a user is empty,
the fallback service `mysql` is used. If a username@host-combination matches
multiple rows, they will all be attempted until authentication succeeds or all
services fail.

PAM service configuration is out of the scope of this document, see
[The Linux-PAM System Administrators' Guide
](http://www.linux-pam.org/Linux-PAM-html/Linux-PAM_SAG.html) for more
information. A simple service definition used for testing this module is below.

```
auth            required        pam_unix.so
account         required        pam_unix.so
```

## Anonymous user mapping

The MaxScale PAM authenticator supports a limited version of
[user mapping](https://mariadb.com/kb/en/library/user-and-group-mapping-with-pam/).
Anonymous mapping is enabled in MaxScale if the following user exists:
- Empty username and wildcard host (`''@'%'`)
- `plugin = 'pam'`
- Proxy grant is on (The query `SHOW GRANTS FOR ''@'%';` returns `GRANT PROXY ON ...`

When the authenticator detects such a user, anonymous account mapping is enabled.
To verify this, search the MaxScale log for "Anonymous PAM user with proxy grant
found. User account mapping enabled." When mapping is on, the PAM authenticator
does not require client accounts to exist in the `mysql.user`-table received from
the backend. It will simply authenticate the client to the local machine with
the username and password supplied. The PAM service used for authentication is
read from the `authentication_string`-field of the anonymous user. If authentication
was successful, MaxScale then uses the username and password to log to the backends.

Anonymous mapping is only attempted if the client username is not found in the
`mysql.user`-table as explained in [Configuration](#configuration). This means,
that if a user is found and the authentication fails, anonymous authentication
is not attempted even when it could use a different PAM service with a different
outcome.

## Implementation details and limitations

The PAM general authentication scheme is difficult for a proxy such as MaxScale.
An application using the PAM interface needs to define a *conversation function*
to allow the OS PAM modules to communicate with the client, possibly exchanging
multiple messages. This works when a client logs in to a normal server, but not
with MaxScale since it needs to autonomously log into multiple backends. For
MaxScale to successfully log into the servers, the messages and answers need to
be predefined. This requirement denies the use of more exotic schemes such as
one-time passwords or two-factor authentication.

The current version of the MaxScale PAM authentication module only supports a
simple password exchange. On the client side, the authentication begins with
MaxScale sending an AuthSwitchRequest packet. In addition to the command, the
packet contains the client plugin name `dialog`, a message type byte `4` and the
message `Password: `. In the next packet, the client should send the password,
which MaxScale will forward to the PAM API running on the local machine. If the
password is correct, an OK packet is sent to the client. No additional
PAM-related messaging is allowed, as this would indicate a more complicated
authentication scheme.

On the backend side, MaxScale expects the servers to act as MaxScale did towards
the client. The servers should send an AuthSwitchRequest packet as defined
above, MaxScale responds with the password received by the client authenticator
and finally backend replies with OK.

## SSL support

PAM Authenticator supports SSL connections from client to MaxScale, but not from
MaxScale to backends.

## Building the module

The PAM authenticator modules require the PAM and SQLite3 development
libraries (libpam0g-dev and sqlite3-dev on Ubuntu).
