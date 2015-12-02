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
address=localhost
port=6603
```

Note that this uses the default port of 6603 and confines the connections to localhost connections only. Remove the address= entry to allow connections from any machine on your network. Changing the port from 6603 will mean that you must allows pass a -p option to the MaxAdmin command.
