# HintRouter

HintRouter was introduced in 2.2 and is still beta.

## Overview

The **HintRouter** module is a simple router intended to operate in conjunction
with the NamedServerFilter. The router looks at the hints embedded in a packet
buffer and attempts to route the packet according to the hint. The user can also
set a default action to be taken when a query has no hints or when the hints
could not be applied.

If a packet has multiple hints attached, the router will read them in order and
attempt routing. Any successful routing ends the process and any further hints
are ignored for the packet.

## Configuration

The HintRouter is a rather simple router and only accepts a few configuration
settings.

```
default_action=<master|slave|named|all>
```

This setting defines what happens when a query has no routing hint or applying
the routing hint(s) fails. If also the default action fails, the routing will
end in error and the session closes. The different values are:

Value  | Description
------ |-----------
master | Route to the master server.
slave  | Route to any single slave server.
named  | Route to a named server. The name is given in the `default_server`-setting.
all    | Default value. Route to all connected servers.

Note that setting default action to anything other than `all` means that session
variable write commands are by default not routed to all backends.

```
default_server=<server-name>
```

Defines the default backend name if `default_action=named`. `<server-name>` must
be a valid backend name.

```
max_slaves=<limit>
```

`<limit>` should be an integer, -1 by default. Defines how many backend slave
servers a session should attempt to connect to. Having less slaves defined in
the services and/or less successful connections during session creation is not
an error. The router will attempt to distribute slaves evenly between sessions
by assigning them in a round robin fashion. The session will always try to
connect to a master regardless of this setting, although not finding one is not
an error.

Negative values activate default mode, in which case this value is set to the
number of backends in the service - 1, so that the sessions are connected to all
slaves.

If the hints or the `default_action` point to a named server, this setting is
probably best left to default to ensure that the specific server is connected to
at session creation. The router will not attempt to connect to additional
servers after session creation.

## Examples

A minimal configuration doesn't require any parameters as all settings have
reasonable defaults.
```
[Routing-Service]
type=service
router=hintrouter
servers=slave1,slave2,slave3
```

If packets should be routed to the master server by default and only a few
connections are required, the configuration might be as follows.
```
[Routing-Service]
type=service
router=hintrouter
servers=MyMaster, slave1,slave2,slave3,slave4,slave5,slave6,slave7
default_action=master
max_slaves=2
```
