# PostgreSQL authentication

This document describes authentication in MaxScale when a listener is configured
to accept PostgreSQL clients, i.e `protocol=postgresql`. PostgreSQL
authentication works quite similarly to MariaDB so much of the information given
in the [main authentication document](Authentication-Modules.md) applies and
should be read prior to this document. The differences are explained below.

## User account management

Similar to MaxScale services handling MariaDB clients, a PostgreSQL service
maintains knowledge of the user accounts defined on the backend servers.
PostgreSQL servers store authentication information in two views. MaxScale
requires access to both to properly authenticate users.

*pg_hba_file_rules* shows the contents of the client authentication
configuration file
[pg_hba.conf](https://www.postgresql.org/docs/15/auth-pg-hba-conf.html). The
contents are thus modified by editing the file itself. Server restart may be
needed to take any changes into use.

*pg_authid* stores role and password information.

### Required grants

The pg_hba.conf-file should have a line that allows the service user (often
"maxscale") to log in to database "postgres". Change the ip to match the
MaxScale host ip or use "all".
```
host    postgres     maxscale    127.0.0.1/32      scram-sha-256
```

The service-user requires extra privileges to read user account information
from the server as the *pg_hba_file_rules* and *pg_authid* views are restricted
by default. The commands below give the "maxscale" user sufficient grants.
Connect to database "postgres" with a privileged user to give the grants.

```
create role maxscale login password 'maxpw';
grant select on pg_hba_file_rules to maxscale;
grant execute on function pg_hba_file_rules to maxscale;
grant select on pg_authid to maxscale;
```

### Authentication methods

MaxScale supports the *trust*, *password* and *scram*
[authentication methods](https://www.postgresql.org/docs/current/auth-methods.html).
The enabled methods are set in the listener configuration similar to MariaDB
authentication plugins.
```
[MyPgListener]
type=listener
port=4007
address=0.0.0.0
service=MyPgService
protocol=postgresql
authenticator=password,scram-sha-256
```
Alternatively, one can set `authenticator=all` to enable all supported methods.

## Limitations and troubleshooting

PostgreSQL services do not support the settings *auth_all_servers*,
*local_address* and *enable_root_user*. Any configured values are ignored.
Authenticator options *skip_authentication* and *match_host* are supported,
*lower_case_table_names* is not.

MaxScale support for PostgreSQL host-based authentication rules (contents of
pg_hba.conf-file) is limited. Only connection types "host", "hostssl", and
"hostnossl" are supported, with all three effectively interpreted as "host".
Group name syntax (`+`) for users is not supported.

Address matching is very limited, with only IPs and "all" supported.
Hostnames are not supported and will not match an incoming client. Netmasks
are ignored. "samehost" and "samenet" are not supported.

Any unsupported entries in the pg_hba.conf-file may result in MaxScale picking
the wrong entry for an incoming client.
