# Configuring the MaxAdmin Administrative Interface

To configure the service which is used by the `maxadmin` command to connect to
MariaDB MaxScale, add the following service and listener sections to the
configuration file.

```
[CLI]
type=service
router=cli

[CLI-Listener]
type=listener
service=CLI
protocol=maxscaled
socket=default
```

This defines a UNIX domain socket which `maxadmin` will use to connect to
MaxScale. MaxAdmin provides monitoring and administration functionality that can
be used to inspect the state of MaxScale.
