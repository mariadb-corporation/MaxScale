# MariaDB MaxScale 2.0.0 Release Notes

Release 2.0.0 is a Beta release.

This document describes the changes in release 2.0.0, when compared to
release 1.4.3.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## License

The license of MaxScale has been changed from GPLv2 to MariaDB BSL.

For more information about MariaDB BSL, please refer to
[MariaDB BSL](https://www.mariadb.com/bsl).

## New Features

### Binlog-to-Avro Translator

The 2.0 release of MaxScale contains the beta release of the binlog-to-Avro
conversion and distribution modules. These modules allow MaxScale to connect to
a MariaDB 10.0 master server and convert the binary log events to Avro format
change records. These records can then be queries as a continuous JSON or raw Avro
stream using the new CDC protocol.

The [Avrorouter Tutorial](../Tutorials/Avrorouter-Tutorial.md) contains
information on how to get started with the binlog-to-Avro translation.

The [Avrorouter](../Routers/Avrorouter.md) documentation has more information
on the details of this conversion process and how to configure the module.

The [CDC Protocol](../Protocols/CDC.md) documentation contains the details of
the new protocol.

### Read Continuation upon Master Down

The _readwritesplit_ routing module now supports a high availability read mode
where read queries are allowed even if the master server goes down. The new
functionality supports three modes: disconnection on master failure, disconnection
on first write after master failure and error on write after master failure.

The MySQL monitor module, _mysqlmon_, now supports stale states for both the master
and slave servers. This means that when a slave loses its master, it will retain
the slave state as long as it is running.

For more details about these new modes, please read the [ReadWriteSplit](../Routers/ReadWriteSplit.md)
and [MySQL Monitor](../Monitors/MySQL-Monitor.md) documentation.

### Backend SSL

The configuration for a backend server can now be set for SSL connections from MaxScale. Although loosely referred to as SSL, this is nowadays the TLS security protocol. If, in MaxScale, a server is configured with SSL parameters then MaxScale will only connect to it using a secure protocol. MaxScale supports TLS versions 1.0, 1.1 and 1.2; which can be used will depend on the capability of the backend server. Once configured, if a secure connection cannot be made, attempts to connect to MaxScale that require that server will fail. An alternative that should be considered is the use of SSH tunnels.

For more information about backend SSL, please refer to
[Server and SSL](../Getting-Started/Configuration-Guide.md#server-and-ssl)

### Connection Throttling

The option now exists to set [max_connections](../Getting-Started/Configuration-Guide.md#max_connections) for a service. If a non-zero number is specified, then MaxScale will accept connection requests only up to the specified limit. Further connections will receive the error message "Too many connections" with error number 1040. .

### MaxAdmin Security Improvements

The way a user of MaxAdmin is authenticated has been completely changed.
In 2.0, MaxAdmin can only connect to MaxScale using a domain socket, thus
_only when run on the same host_, and authorization is based upon the UNIX
identity. Remote access is no longer supported.

When 2.0 has been installed, MaxAdmin can only be used by `root` and
other users must be added anew. Please consult
[MaxAdmin documentation](../Reference/MaxAdmin.md) for more details.

### Query Classifier

The query classifier component that MaxScale uses when deciding what
to do with a particular query has been changed. It used to be based
upon the MariaDB embedded library, but is now based upon sqlite3.
This change should not cause any changes in the behaviour of MaxScale.

For more information, please refer to
[Configuration Guide](../Getting-Started/Configuration-Guide.md#query_classifier).

## Bug fixes

[Here is a list of bugs fixed since the release of MaxScale 1.4.3.](https://jira.mariadb.org/browse/MXS-739?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20resolution%20in%20(Fixed%2C%20Done)%20AND%20fixVersion%20%3D%202.0.0)

 * [MXS-821](https://jira.mariadb.org/browse/MXS-821): filestem router option for binlog router is not documented
 * [MXS-814](https://jira.mariadb.org/browse/MXS-814): Service and monitor permission checks only use the last available server
 * [MXS-813](https://jira.mariadb.org/browse/MXS-813): binlogrouter, mariadb10.0, signal 11, crash
 * [MXS-801](https://jira.mariadb.org/browse/MXS-801): strip_db_esc should default to True
 * [MXS-790](https://jira.mariadb.org/browse/MXS-790): replication_heartbeat table privilege is not checked / fails silently / is not documented
 * [MXS-776](https://jira.mariadb.org/browse/MXS-776): Documentation about limitations of reload config is not clear
 * [MXS-772](https://jira.mariadb.org/browse/MXS-772): RPM installation produces errors
 * [MXS-766](https://jira.mariadb.org/browse/MXS-766): R/W router sends DEALLOCATE PREPARE to ALL instead of MASTER
 * [MXS-739](https://jira.mariadb.org/browse/MXS-739): Maxinfo issuing invalid null's in JSON response
 * [MXS-733](https://jira.mariadb.org/browse/MXS-733): MaxScale `list sessions` can report "Invalid State" for some sessions.
 * [MXS-720](https://jira.mariadb.org/browse/MXS-720): MaxScale fails to start and doesn't log any useful message when there are spurious characters in the config file
 * [MXS-718](https://jira.mariadb.org/browse/MXS-718): qc_mysqlembedded does not report fields for INSERT
 * [MXS-704](https://jira.mariadb.org/browse/MXS-704): start/stop scripts use which in a non-silent manner
 * [MXS-695](https://jira.mariadb.org/browse/MXS-695): MaxScale does not build on Debian 8 following build from source instructions
 * [MXS-685](https://jira.mariadb.org/browse/MXS-685): 1.4.1: ReadWrite Split on Master-Master setup doesn't chose master, logs "RUNNING MASTER" error message instead (related to MXS-511?)
 * [MXS-675](https://jira.mariadb.org/browse/MXS-675): QLA Filter Output Log Improvements
 * [MXS-658](https://jira.mariadb.org/browse/MXS-658): Crash in embedded library when MariaDB 10.0 is used
 * [MXS-653](https://jira.mariadb.org/browse/MXS-653): maxpasswd writes notice message to stdout
 * [MXS-652](https://jira.mariadb.org/browse/MXS-652): ssl is configured in a wrong way, but Maxscale can be started and works
 * [MXS-633](https://jira.mariadb.org/browse/MXS-633): Galera Monitor should not require the REPLICATION CLIENT privilege
 * [MXS-631](https://jira.mariadb.org/browse/MXS-631): Rename and clean up macros.cmake
 * [MXS-477](https://jira.mariadb.org/browse/MXS-477): readconnroute misinterprets data as COM_CHANGE_USER
 * [MXS-419](https://jira.mariadb.org/browse/MXS-419): Socket creation failed due 24, Too many open files.

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

The source code is available [here](https://github.com/mariadb-corporation/maxscale-bsl).
