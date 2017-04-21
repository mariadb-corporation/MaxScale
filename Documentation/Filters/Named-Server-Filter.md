# Named Server Filter

## Overview

The **namedserverfilter** is a filter module for MariaDB MaxScale which is able to route queries to servers based on regular expression matches.

## Configuration

The configuration block for the Named Server filter requires the minimal filter options in its section within the maxscale.cnf file, stored in /etc/maxscale.cnf.

```
[NamedServerFilter]
type=filter
module=namedserverfilter
match=some string
server=server2

[MyService]
type=service
router=readwritesplit
servers=server1,server2
user=myuser
passwd=mypasswd
filters=NamedServerFilter
```

## Filter Options

The named server filter accepts the following options.

|Option    |Description                                 |
|----------|--------------------------------------------|
|ignorecase|Use case-insensitive matching               |
|case      |Use case-sensitive matching                 |
|extended  |Use extended regular expression syntax (ERE)|

To use multiple filter options, list them in a comma-separated list.

```
options=case,extended
```

**Note:** The _ignorecase_ and _case_ options are mutually exclusive and only
one of them should be used.

## Filter Parameters

The named server filter requires two mandatory parameters to be defined.

### `match`

A parameter that can be used to match text in the SQL statement which should be replaced.

```
match=TYPE[	]*=
```

If the filter option ignorecase is used all regular expressions are evaluated with the option to ignore the case of the text, therefore a match option of select will match both type, TYPE and any form of the word with upper or lowercase characters.

### `server`

This is the server where matching queries will be router. The server should be in use by the service which uses this filter.

```
server=server2
```

### `source`

The optional source parameter defines an IP address that is used to match against the address from which the client connection to MariaDB MaxScale originates. Only sessions that originate from this IP address will have the match and replacement applied to them.

```
source=127.0.0.1
```
Since MaxScale 2.1 it's also possible to use % wildcards:

```
source=192.%.%.%
source=192.168.%.%
source=192.168.10.%
```
Please note that using source=% to match any IP it's not allowed.

### `user`

The optional user parameter defines a user name that is used to match against the user from which the client connection to MariaDB MaxScale originates. Only sessions that are connected using this username will have the match and replacement applied to them.

```
user=john
```

## Examples

### Example 1 - Route queries targeting a specific table to a server

This will route all queries matching the regular expression ` *from *users` to the server named *server2*. The filter will ignore character case in queries.

A query like `SELECT * FROM users` would be routed to server2 where as a query like `SELECT * FROM accounts` would be routed according to the normal rules of the router.

```
[NamedServerFilter]
type=filter
module=namedserverfilter
match= *from *users
options=ignorecase
server=server2

[MyService]
type=service
router=readwritesplit
servers=server1,server2
user=myuser
passwd=mypasswd
filters=NamedServerFilter
```
