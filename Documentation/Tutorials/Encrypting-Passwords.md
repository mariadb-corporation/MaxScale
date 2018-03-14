# Encrypting Passwords

There are two options for representing the password, either plain text or
encrypted passwords may be used. In order to use encrypted passwords a set of
keys must be generated that will be used by the encryption and decryption
process. To generate the keys use the `maxkeys` command and pass the name of the
secrets file in which the keys are stored.

```
maxkeys /var/lib/maxscale/.secrets
```

Once the keys have been created the `maxpasswd` command can be used to generate
the encrypted password.

```
maxpasswd plainpassword
96F99AA1315BDC3604B006F427DD9484
```

The username and password, either encrypted or plain text, are stored in the
service section using the `user` and `password` parameters.

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
