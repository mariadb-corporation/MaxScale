# Named Server Filter

## Overview

The **namedserverfilter** is a MariaDB MaxScale filter module able to route
queries to servers based on regular expression  (regex) matches. Since it is a
filter instead of a router, the NamedServerFilter only sets routing suggestions.
It requires a compatible router to be effective. Currently, both
**readwritesplit** and **hintrouter** take advantage of routing hints in the
data packets. This filter uses the *PCRE2* library for regular expression
matching.

## Configuration

The filter accepts settings in two modes: *legacy* and *indexed*. Only one of
the modes may be used for a given filter instance. The legacy mode is meant for
backwards compatibility and allows only one regular expression and one server
name in the configuration. In indexed mode, up to 25 regex-server pairs are
allowed in the form *match01* - *target01*, *match02* - *target02* and so on.
Also, in indexed mode, the server names (targets) may contain a list of names or
special tags `->master` or `->slave`.

Below is a configuration example for the filter in indexed-mode. The legacy mode
is not recommeded and may be removed in a future release. In the example, a
SELECT on TableOne (*match01*) results in routing hints to two named servers,
while a SELECT on TableTwo is suggested to be routed to the master server of the
service. Whether a list of server names is interpreted as a route-to-any or
route-to-all is up to the attached router. The HintRouter sees a list as a
suggestion to route-to-any. For additional information on hints and how they can
also be embedded into SQL-queries, see
[Hint-Syntax](../Reference/Hint-Syntax.md).

```
[NamedServerFilter]
type=filter
module=namedserverfilter
match01=^Select.*TableOne$
target01=server2,server3
match22=^SELECT.*TableTwo$
target22=->master

[MyService]
type=service
router=readwritesplit
servers=server1,server2,server3
user=myuser
passwd=mypasswd
filters=NamedServerFilter
```

## Filter Parameters

The NamedServerFilter requires two mandatory parameters.

### `matchXY`

Regular expression the SQL-query is matched against. XY must be a number in the
range 01 - 25. Each *match* setting must have a similarly indexed *target*
setting.

```
match01=^SELECT
```

### `targetXY`

This is the hint which will be attached to the queries matching the regex. If a
compatible router is used in the service the query will be routed accordingly.
The target can be one of the following:

 * a server name (adds a `HINT_ROUTE_TO_NAMED_SERVER` hint)
 * a list of server names, comma-separated (adds several
 `HINT_ROUTE_TO_NAMED_SERVER` hints)
 * `->master` (adds a `HINT_ROUTE_TO_MASTER` hint)
 * `->slave` (adds a `HINT_ROUTE_TO_SLAVE` hint)
 * `->all` (adds a `HINT_ROUTE_TO_ALL` hint)

```
target01=MyServer2
```

### `source`

This optional parameter defines an IP address or mask which a connecting
client's IP address is matched against. Only sessions whose address matches this
setting will have this filter active and performing the regex matching. Traffic
from unmatching client IPs is simply left as is and routed straight through.

```
source=127.0.0.1
```
Since MaxScale 2.1 it's also possible to use % wildcards:

```
source=192.%.%.%
source=192.168.%.%
source=192.168.10.%
```
Note that using `source=%` to match any IP is not allowed.

Since MaxScale 2.3 it's also possible to specify multiple addresses separated
by comma. Incoming client connections are subsequently checked against each.
```
source=192.168.21.3,192.168.10.%
```

### `user`

This optional parameter defines a user name the connecting client username is
matched against. Only sessions that are connected using this username will have
the match and routing hints applied to them. Traffic from unmatching client user
names is simply left as is and routed straight through.

```
user=john
```

## Filter Options

The named server filter accepts the following options.

|Option    |Description                                 |
|----------|--------------------------------------------|
|ignorecase|Use case-insensitive matching (default)     |
|case      |Use case-sensitive matching                 |
|extended  |Ignore white space and # comments           |

To use multiple filter options, list them in a comma-separated list.

```
options=case,extended
```

**Note:** The *ignorecase* and *case* options are mutually exclusive and only
one of them should be used.

## Notes

The maximum number of accepted *match* - *target* pairs may be higher and can
change if other features are added to the filter. A minimum of 25 is guaranteed
for now.

In the configuration, the indexed match and target settings may be in any order
and may skip numbers. During SQL-query matching, however, the regexes are tested
in ascending order: match01, match02, match03 and so on. As soon as a match is
found for a qiven query, the routing hints are written and the packet is
forwarded to the next filter or router. Any possibly remaining match regexes are
ignored. This means the *match* - *target* pairs should be indexed in priority
order, or, if priority is not a factor, in order of decreasing match
probability.

## Examples

### Example 1 - Route queries targeting a specific table to a server

This will route all queries matching the regular expression ` *from *users` to
the server named *server2*. The filter will ignore character case in queries.

A query like `SELECT * FROM users` would be routed to server2 where as a query
like `SELECT * FROM accounts` would be routed according to the normal rules of
the router.

```
[NamedServerFilter]
type=filter
module=namedserverfilter
match02= *from *users
target02=server2

[MyService]
type=service
router=readwritesplit
servers=server1,server2
user=myuser
passwd=mypasswd
filters=NamedServerFilter
```
