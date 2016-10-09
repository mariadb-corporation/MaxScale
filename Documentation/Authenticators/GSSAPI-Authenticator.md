# GSSAPI Client Authenticator

GSSAPI is an authentication protocol that is commonly implemented with
Kerberos on Unix or Active Directory on Windows. This document describes
the GSSAPI authentication in MaxScale.

The _GSSAPIAuth_ module implements the client side authentication and the
_GSSAPIBackendAuth_ module implements the backend authentication.

## Authenticator options

The client side GSSAPIAuth authenticator supports one option, the service
principal name that MaxScale sends to the client. The backend authenticator
module has no options.

### `principal_name`

The service principal name to send to the client. This parameter is a
string parameter which is used by the client to request the token.

The default value for this option is _mariadb/localhost.localdomain_.

The parameter must be a valid GSSAPI principal name
e.g. `styx/pluto@EXAMPLE.COM`. The principal name can also be defined
without the realm part in which case the default realm will be used.

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
