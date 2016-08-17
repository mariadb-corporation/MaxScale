# Gatekeeper Filter

# Overview

This filter will learn from input data read during a learning phase. After
learning the characteristics of the input, the filter can then be set into
the enforcing mode. In this mode the filter will block any queries that do
not conform to the training set. This is a simple yet effective way to prevent
unwanted queries from being executed in the database.

When the filter receives a query in the learning mode, the filter converts it
into a canonicalized form i.e. creates a query digest. The query digest is then
stored for later use.

When the filter is started in enforcing mode, the list of stored query digests
is used as a master list of allowed queries. The filter calculates a query
digest for all incoming queries and compares it to the master list. If the
query digest is in the master list, the query is executed normally. If it
isn't found from the list, the query is not executed, an error is returned
to the client and a warning message is logged.

# Configuration

## Filter Parameters

### `mode`

This is a mandatory parameter for the filter which controls the filter's operation.

Accepted values are _learn_ and _enforce_. _learn_ sets the filter into the
learning mode, where the query digests are calculated and stored at the end of
each session. _enforce_ sets the filter into the enforcement mode where incoming
queries are compared to the previously calculated list of query digests.

### `datadir`

This is an optional parameter which controls where the calculated query digests
are stored. This parameter takes an absolute path to a directory as its argument.

## Example Configuration

Here is an example configuration of the filter with both the mandatory _mode_
and the optional _datadir_ parameters defined. The filter is in learning mode
and after collecting enough samples, the filter is should be set into enforcing
mode. The filter stores the gathered query digests in `/var/lib/maxscale/gatekeeper/`.

```
[Smart Firewall]
type=filter
module=gatekeeper
mode=learn
datadir=/var/lib/maxscale/gatekeeper/
```
