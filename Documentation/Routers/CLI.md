# CLI

The command line interface as used by `maxadmin`. This is a variant of the debugcli that is built slightly differently so that it may be accessed by the client application `maxadmin`. The CLI requires the use of the `maxscaled` protocol.

## Configuration

There are two components to the definition required in order to run the command line interface to use with MaxAdmin; a service and a listener.

The default entries required are shown below.

```
[CLI]
type=service
router=cli

[CLI Listener]
type=listener
service=CLI
protocol=maxscaled
socket=default
```
