# Psreuse

The `psreuse` filter reuses identical prepared statements inside the same client
connection. This filter only works with binary protocol prepared statements and
not with text protocol prepared statements executed with the `PREPARE` SQL
command.

When this filter is enabled and the connection prepares an identical prepared
statement multiple times, instead of preparing it on the server the existing
prepared statement handle is reused. This also means that whenever prepared
statements are closed by the client, they will be left open by readwritesplit.

Enabling this feature will increase memory usage of a session. The amount of
memory stored per prepared statement is proportional to the length of the
prepared SQL statement and the number of parameters the statement has.

# Configuration

To add the filter to a service, define an instance of the filter and then add it
to a service's `filters` list:

```
[PsReuse]
type=filter
module=psreuse

[MyService]
...
filters=PsReuse
```

# Limitations

- If the SQL in the prepared statement is larger than 1677723 bytes, the
  prepared statement will not be cached.

- If the same SQL is prepared more than once at the same time, only one of them
  will succeed. This happens as the prepared statement reuse uses the SQL string
  in the comparison to detect if a statement is already prepared.