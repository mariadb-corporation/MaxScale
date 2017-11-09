# MariaDB MaxScale 2.2.1 Release Notes

Release 2.2.1 is a Beta release.

This document describes the changes in release 2.2.1, when compared to
release 2.2.0.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

### Process identity

By default, MaxScale can no longer be run as `root`, but must be run as some
other user. However, it is possible to start MaxScale as `root`, as long as
the user to run MaxScale as is provided as a command line argument:
```
root@host:~# maxscale --user=maxuser ...
```
If it is imperative to run MaxScale as root, e.g. in a Docker container, it
can be achieved by invoking MaxScale as root and by explicitly specifying
the user to also be root:
```
root@host:~# maxscale --user=root ...
```

### Binlog server

* The `mariadb10_slave_gtid` parameter was removed and slave connections can now
  always register with MariaDB 10 GTID.

* The `binlog_structure` parameter was removed and the binlogs are stored
  automatically in 'tree' mode when `mariadb10_master_gtid` is enabled.

* If `mariadb10_master_gtid` is enabled, the `transaction_safety` is
  automatically enabled. In MaxScale 2.2.0, if `transaction_safety` was disabled
  when `mariadb10_master_gtid` was enabled MaxScale would refuse to start.

## Dropped Features

## New Features

### REST API Relationship Endpoints

The _servers_, _monitors_ and _services_ types now support direct updating of
relationships via the `relationships` endpoints. This conforms to the JSON API
specification on updating resource relationships.

For more information, refer to the REST API documentation. An example of this
can be found in the
[Server Resource documentation](../REST-API/Resources-Server.md#update-server-relationships).

### PL/SQL Comaptibility

The parser of MaxScale has been extended to support the PL/SQL compatibility
features of the upcoming 10.3 release. For more information on how to enable
this mode, please refer to the
[configuration guide](../Getting-Started/Configuration-Guide.md#sql_mode).

This functionality was available already in MaxScale 2.2.0.

### Environment Variables in the configuration file

If the global configuration entry `substitute_variables` is set to true,
then if the first character of a value in the configuration file is a `$`
then everything following that is interpreted as an environment variable
and the configuration value is replaced with the value of the environment
variable. For more information please consult the
[Configuration Guide](../Getting-Started/Configuration-Guide.md).

## Bug fixes

[Here is a list of bugs fixed in MaxScale 2.2.1.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.2.1)

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the Linux distributions supported
by MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/resources/downloads).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is X.Y.Z. Further, *master* always refers to the latest released non-beta version.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
