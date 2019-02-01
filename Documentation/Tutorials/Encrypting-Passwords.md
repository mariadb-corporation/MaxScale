# Encrypting Passwords

There are two options for representing the password, either plain text or
encrypted passwords may be used. In order to use encrypted passwords a set of
keys must be generated that will be used by the encryption and decryption
process. To generate the keys, use the `maxkeys` command.

```
maxkeys
```

By default the key file will be generated in `/var/lib/maxscale`. If a different
directory is required, it can be given as the first argument to the program. For
more information, see `maxkeys --help`.

Once the keys have been created the `maxpasswd` command can be used to generate
the encrypted password.

```
maxpasswd plainpassword
96F99AA1315BDC3604B006F427DD9484
```

The username and password, either encrypted or plain text, are stored in the
service section using the `user` and `password` parameters.

If a custom location was used for the key file, give it as the first argument to
`maxpasswd` and pass the password to be encrypted as the second argument. For
more information, see `maxkeys --help`.

Here is an example configuration that uses an encrypted password.

```
[My-Service]
type=service
router=readconnroute
router_options=master
servers=dbserv1, dbserv2, dbserv3
user=maxscale
password=96F99AA1315BDC3604B006F427DD9484
```

If the key file is not in the default location, the
[`datadir`](../Getting-Started/Configuration-Guide.md#datadir) parameter must be
set to the directory that contains it.
