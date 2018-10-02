# GSSAPI Client Authenticator

GSSAPI is an authentication protocol that is commonly implemented with
Kerberos on Unix or Active Directory on Windows. This document describes
the GSSAPI authentication in MaxScale.

The _GSSAPIAuth_ module implements the client side authentication and the
_GSSAPIBackendAuth_ module implements the backend authentication.

## Preparing the GSSAPI system

For Unix systems, the usual GSSAPI implementation is Kerberos. This is a short
guide on how to set up Kerberos for MaxScale.

The first step is to configure MariaDB to use GSSAPI authentication. The MariaDB
documentation for the
[GSSAPI Authentication Plugin](https://mariadb.com/kb/en/mariadb/gssapi-authentication-plugin/)
is a good example on how to set it up.

The next step is to copy the keytab file from the server where MariaDB is
installed to the server where MaxScale is located. The keytab file must be
placed in the configured default location which almost always is
`/etc/krb5.keytab`.

To take GSSAPI authentication into use, add the following to the listener.

```
authenticator=GSSAPIAuth
authenticator_options=principal_name=mariadb/localhost.localdomain@EXAMPLE.COM
```

Change the principal name to the same value you configured for the MariaDB
server.

After the listeners are configured, add the following to all servers that use GSSAPI users.

```
authenticator=GSSAPIBackendAuth
```

## Authenticator options

The client side GSSAPIAuth authenticator supports one option, the service
principal name that MaxScale sends to the client. The backend authenticator
module has no options.

### `principal_name`

The service principal name to send to the client. This parameter is a string
parameter which is used by the client to request the token. The default value
for this option is _mariadb/localhost.localdomain_.

This parameter *must* be the same as the principal name that the backend MariaDB
server uses.

## Implementation details

Read the [Authentication Modules](Authentication-Modules.md) document for more
details on how authentication modules work in MaxScale.

### GSSAPI authentication

The GSSAPI plugin authentication starts when the database server sends the
service principal name in the AuthSwitchRequest packet. The principal name will
usually be in the form `service@REALM.COM`.

The client will then request a token for this service from the GSSAPI server and
send the token to the database server. The database server will verify the
authenticity of the token by contacting the GSSAPI server and if the token is
authentic, the server sends the final OK packet.

## Limitations

Client side GSSAPI authentication is only supported when the backend
connections use GSSAPI authentication.

See the [Limitations](../About/Limitations.md) document for more details.

## Building the module

The GSSAPI authenticator modules require the GSSAPI and the SQLite3 development
libraries (krb5-devel and sqlite-devel on CentOS 7).
