# Debug CLI

The **debugcli** router is a special kind of statement based router. Rather than direct the statements at an external data source they are handled internally. These statements are simple text commands and the results are the output of debug commands within MariaDB MaxScale. The service and listener definitions for a debug cli service only differ from other services in that they require no backend server definitions.

## Configuration

The definition of the debug cli service is illustrated below

```
[Debug Service]
type=service
router=debugcli

[Debug Listener]
type=listener
service=Debug Service
protocol=telnetd
port=4442
```

Connections using the telnet protocol to port 4442 of the MariaDB MaxScale host will result in a new debug CLI session. A default username and password are used for this module, new users may be created using the administrative interface. As soon as any users are explicitly created the default username will no longer continue to work. The default username is `admin` with a password of `mariadb`.
