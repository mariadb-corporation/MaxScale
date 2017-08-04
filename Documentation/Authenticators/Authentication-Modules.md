# Authentication Modules in MaxScale

This document describes the modular authentication in MaxScale. It contains
protocol specific information on authentication and how it is handled in
MaxScale.

The constants described in this document are defined in the authenticator.h
header unless otherwise mentioned.

Authenticator modules compatible with MySQL protocol in MaxScale are
[MySQL](MySQL-Authenticator.md), [GSSAPI](GSSAPI-Authenticator.md) and
[PAM](PAM-Authenticator.md).

## Authenticator initialization

When the authentication module is first loaded, the `initialize` entry point is
called. The return value of this function will be passed as the first argument
to the other entry points.

The `loadUsers` entry point of the client side authenticator is called when a
service starts. The authenticator can load external user data when this entry
point is called. This entry point is also called when user authentication has
failed and the external user data needs to be refreshed.

When a connection is created, the `create` entry point is called to create per
connection data. The return value of this function is stored in the
`dcb->authenticator_data` field of the DCB object. This data is freed in the
`destroy` entry point and the value returned by `create` will be given as the
first parameter.

# MySQL Authentication Modules

The MySQL protocol authentication starts when the server sends the handshake
packet to the client to which the client responds with a handshake response
packet. If the server is using the default *mysql_native_password*
authentication plugin, the server responds with either an OK packet or an ERR
packet and the authentication is complete.

If a different authentication plugin is required to complete the authentication,
instead of sending an OK or ERR packet, the server responds with an
AuthSwitchRequest packet. This is where the pluggable authentication in MaxScale
starts.

## Client authentication in MaxScale

The first packet the client side authenticator plugins will receive is the
client's handshake response packet.

The client protocol module will call the `extract` entry point of the
authenticator where the authenticator should extract client information. If the
`extract` entry point returns MXS_AUTH_SUCCEEDED, the `authenticate` entry point
will be called.

The `authenticate` entry point is where the authenticator plugin should
authenticate the client. If authentication is successful, the `authenticate`
entry point should return MXS_AUTH_SUCCEEDED. If authentication is not yet
complete or if the authentication module should be changed, the `authenticate`
entry point should return MXS_AUTH_INCOMPLETE.

Authenticator plugins which do not use the default *mysql_native_password*
authentication plugin should send an AuthSwitchRequest packet to the client and
return MXS_AUTH_INCOMPLETE. When more data is available, the `extract` and
`authenticate` entry points will be called again.

If either of the aforementioned entry points returns one of the following
constants, the authentication is considered to have failed and the session will
be closed.

- MXS_AUTH_FAILED
- MXS_AUTH_FAILED_DB
- MXS_AUTH_FAILED_SSL

Read the individual authenticator module documentation for more details on the
authentication process of each authentication plugin.

## Backend authentication in MaxScale

The first packet the authentication plugins in MaxScale will receive is either
the AuthSwitchRequest packet or, in case of _mysql_native_password_, the OK
packet. At this point, the protocol plugin will call the `extract` entry point
of the backend authenticator. If the return value of the call is one of the
following constants, the protocol plugin will call the `authenticate` entry
point of the authenticator.

- MXS_AUTH_SUCCEEDED
- MXS_AUTH_INCOMPLETE

If the `authenticate` entry point returns MXS_AUTH_SUCCEEDED, then
authentication is complete and any queued queries from the clients will be sent
to the backend server. If the return value is MXS_AUTH_INCOMPLETE or
MXS_AUTH_SSL_INCOMPLETE, the protocol module will continue the authentication by
calling the `extract` entry point once more data is available.

If either of the aforementioned entry points returns one of the following
constants, the authentication is considered to have failed and the session will
be closed.

- MXS_AUTH_FAILED
- MXS_AUTH_FAILED_DB
- MXS_AUTH_FAILED_SSL

Read the individual authenticator module documentation for more details on the
authentication process of each authentication plugin.
