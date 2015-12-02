# Debug CLI

The **debugcli** router is a special kind of statement based router. Rather than direct the statements at an external data source they are handled internally. These statements are simple text commands and the results are the output of debug commands within MaxScale. The service and listener definitions for a debug cli service only differ from other services in that they require no backend server definitions.

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

Connections using the telnet protocol to port 4442 of the MaxScale host will result in a new debug CLI session. A default username and password are used for this module, new users may be created using the add user command. As soon as any users are explicitly created the default username will no longer continue to work. The default username is admin with a password of mariadb.

The debugcli supports two modes of operation, `developer` and `user`. The mode is set via the `router_options` parameter. The user mode is more suited to end-users and administrators, whilst the develop mode is explicitly targeted to software developing adding or maintaining the MaxScale code base. Details of the differences between the modes can be found in the debugging guide for MaxScale. The default is `user` mode. The following service definition would enable a developer version of the debugcli.

```
[Debug Service]
type=service
router=debugcli
router_options=developer
```

It should be noted that both `user` and `developer` instances of debugcli may be defined within the same instance of MaxScale, however they must be defined as two distinct services, each with a distinct listener.

```
[Debug Service]
type=service
router=debugcli
router_options=developer

[Debug Listener]
type=listener
service=Debug Service
protocol=telnetd
port=4442

[Admin Service]
type=service
router=debugcli

[Admin Listener]
type=listener
service=Debug Service
protocol=telnetd
port=4242
```
