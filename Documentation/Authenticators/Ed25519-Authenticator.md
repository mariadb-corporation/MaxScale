# Ed25519 Authenticator

[TOC]

Ed25519 is a highly secure authentication method based on public key
cryptography. It is used with the auth_ed25519-plugin of MariaDB Server.

When a client authenticates via ed25519, MaxScale first sends them a random
message. The client signs the message using their password as private key and
sends the signature back. MaxScale then checks the signature using the public
key fetched from the *mysql.user*-table. The client password or an equivalent
token is never exposed. For more information, see
[server documentation](https://mariadb.com/kb/en/authentication-plugin-ed25519/).

The security of this authentication scheme presents a problem for a proxy such
as MaxScale since MaxScale needs to log in to backend servers on behalf of the
client. Since each server will generate their own random messages, MaxScale
cannot simply forward the original signature. Either the real password is
required, or a different authentication scheme must be used between MaxScale
and backends. The MaxScale ed25519auth-plugin supports both alternatives.

## Configuration

To begin, add "ed25519auth" to the list of authenticators for a listener.
```
[Read-Write-Listener]
type=listener
address=::
service=Read-Write-Service
authenticator=ed25519auth
```

MaxScale will now authenticate incoming clients with ed25519 if their user
account has *plugin* set to "ed25519" in the *mysql.user*-table. However,
routing queries will fail since MaxScale cannot authenticate to backends. To
continue, either use a mapping file or enable sha256-mode. Sha256-mode is
enabled with the following settings.

### `ed_mode`

This setting defines the authentication mode used. Two values are supported:

- `ed25519` (default) Digital signature based authentication. Requires mapping
for backend support.
- `sha256` Authenticate client with caching_sha2_password-plugin instead.
Requires either SSL or configured RSA-keys.

```
authenticator_options=ed_mode=sha256
```

### `ed_rsa_privkey_path` and `ed_rsa_pubkey_path`

Defines the RSA-keys used for encrypting the client password if SSL is not in
use. Should point to files with the private and public keys.

```
authenticator_options=ed_mode=sha256,
 ed_rsa_privkey_path=/tmp/sha_private_key.pem,
 ed_rsa_pubkey_path=/tmp/sha_public_key.pem
```

## Using a mapping file

To enable MaxScale to authenticate to backends,
[user mapping](../Getting-Started/Configuration-Guide.md#user_mapping_file)
can be used. The mapping and backend passwords are given in a json-file.
The client can map to an identical username or to another user, and the backend
authentication scheme can be something else than ed25519.

The following example maps user "alpha" to "beta" and MaxScale then uses
standard authentication to log into backends as "beta". User "alpha"
authenticates to MaxScale using whatever method configured in the server.
User "gamma" does not map to another user, just the password is given.

MaxScale configuration:
```
[Read-Write-Listener]
type=listener
address=::
service=Read-Write-Service
authenticator=ed25519auth,mariadbauth
user_mapping_file=/home/joe/mapping.json
```
/home/joe/mapping.json:
```
{
    "user_map": [
        {
            "original_user": "alpha",
            "mapped_user": "beta"
        },
        {
            "original_user": "gamma",
            "mapped_user": "gamma"
        }
    ],
    "server_credentials": [
        {
            "mapped_user": "beta",
            "password": "hunter2",
            "plugin": "mysql_native_password"
        },
        {
            "mapped_user": "gamma",
            "password": "letmein",
            "plugin": "ed25519"
        }
    ]
}
```

## Using sha256-authentication

The mapping-based solution requires the DBA to maintain a file with user
passwords, which has security and upkeep implications. To avoid this,
MaxScale can instead use the caching_sha2_password-plugin to authenticate
the client. This authentication scheme transmits the client password to MaxScale
in full, allowing MaxScale to log into backends using ed25519. MaxScale
effectively lies to the client about its authentication plugin and then uses
the correct plugin with the backends. Enable sha256-authentication by setting
authentication option *ed_mode* to "sha256".

sha256-authentication is best used with encrypted connections. The example
below shows a listener configured for sha256-mode and SSL.

```
[Read-Write-Listener]
type=listener
address=::
service=Read-Write-Service
authenticator=ed25519auth
authenticator_options=ed_mode=sha256
ssl=true
ssl_key=/tmp/my-key.pem
ssl_cert=/tmp/my-cert.pem
ssl_ca=/tmp/myCA.pem
```

If SSL is not in use, caching_sha2_password transmits the password using
RSA-encryption. In this case, MaxScale needs the public and private RSA-keys.
MaxScale sends the public key to the client if they don't already have it and
the client uses it to encrypt the password. MaxScale then uses the private key
to decrypt the password. The example below shows a listener configured for
sha256-mode without SSL.

```
[Read-Write-Listener]
type=listener
address=::
service=Read-Write-Service
authenticator=ed25519auth
authenticator_options=ed_mode=sha256,
 ed_rsa_privkey_path=/tmp/sha_private_key.pem,
 ed_rsa_pubkey_path=/tmp/sha_public_key.pem
```

The keyfiles can be generated with OpenSSL using the following commands.
```
openssl genrsa -out sha_private_key.pem 2048
openssl rsa -in sha_private_key.pem -pubout -out sha_public_key.pem
```
