# Changelog

## MariaDB MaxScale 2.2

* Limited support from Pluggable Authentication Modules (PAM).
* Proxy protocol support for backend connections.
* REST-API for obtaining information about and for manipulating the
  resources of MaxScale.
* MaxCtrl, a new command line client for administering MaxScale
  implemented in terms of the REST-API.
* Firewall can now prevent the use of functions in conjunction with
  certain columns.
* Parser of MaxScale extended to support window functions and CTEs.
* Parser of MaxScale extended to support PL/SQL compatibility features
  of upcoming 10.3 release.
* Prepared statements are now parsed and the execution of read only
  ones will be routed to slaves.
* Server states are persisted, so in case of crash and restart MaxScale
  has the correct server state quicker.
* Monitor scripts are executed synchronously, so they can safely perform
  actions that change the server states.
* The Masking filter can now both obfuscate and partially mask columns.
* Binlog router supports MariaDB 10 GTID at both ends.
* KILL CONNECTION can now be used through MaxScale.
* Environment variables can now be used in the MaxScale configuration file.
* By default, MaxScale can no longer be run as root.
* The MySQL Monitor is now capable of performing failover and switchover of
  the master. There is also limited capability for rejoining nodes.

For more details, please refer to:
* [MariaDB MaxScale 2.2.7 Release Notes](Release-Notes/MaxScale-2.2.7-Release-Notes.md)
* [MariaDB MaxScale 2.2.6 Release Notes](Release-Notes/MaxScale-2.2.6-Release-Notes.md)
* [MariaDB MaxScale 2.2.5 Release Notes](Release-Notes/MaxScale-2.2.5-Release-Notes.md)
* [MariaDB MaxScale 2.2.4 Release Notes](Release-Notes/MaxScale-2.2.4-Release-Notes.md)
* [MariaDB MaxScale 2.2.3 Release Notes](Release-Notes/MaxScale-2.2.3-Release-Notes.md)
* [MariaDB MaxScale 2.2.2 Release Notes](Release-Notes/MaxScale-2.2.2-Release-Notes.md)
* [MariaDB MaxScale 2.2.1 Release Notes](Release-Notes/MaxScale-2.2.1-Release-Notes.md)
* [MariaDB MaxScale 2.2.0 Release Notes](Release-Notes/MaxScale-2.2.0-Release-Notes.md)

## MariaDB MaxScale 2.1
* MariaDB MaxScale is licensed under MariaDB BSL 1.1.
* Hierarchical configuration files are now supported.
* Logging is now performed in a way compatible with logrotate(8).
* Persistent connections are reset upon resuse.
* Galera monitor now consistently chooses the same node as master.
* Galera Monitor can set the preferred donor nodes list.
* The configuration can now be altered dynamically and the changes are persisted.
* There is now a monitor for Amazon Aurora clusters.
* MySQL Monitor now has a multi-master mode.
* MySQL Monitor now has a failover mode.
* Named Server Filter now supports wildcards for source option.
* Binlog Server can now be configured to encrypt binlog files.
* New filters, _cache_, _ccrfilter_, _insertstream_, _masking_, and _maxrows_ are introduced.
* GSSAPI based authentication can be used
* Prepared statements are now in the database firewall filtered exactly like non-prepared
  statements.
* The firewall filter can now filter based on function usage.
* MaxScale now supports IPv6

For more details, please refer to:
* [MariaDB MaxScale 2.1.17 Release Notes](Release-Notes/MaxScale-2.1.17-Release-Notes.md)
* [MariaDB MaxScale 2.1.16 Release Notes](Release-Notes/MaxScale-2.1.16-Release-Notes.md)
* [MariaDB MaxScale 2.1.15 Release Notes](Release-Notes/MaxScale-2.1.15-Release-Notes.md)
* [MariaDB MaxScale 2.1.14 Release Notes](Release-Notes/MaxScale-2.1.14-Release-Notes.md)
* [MariaDB MaxScale 2.1.13 Release Notes](Release-Notes/MaxScale-2.1.13-Release-Notes.md)
* [MariaDB MaxScale 2.1.12 Release Notes](Release-Notes/MaxScale-2.1.12-Release-Notes.md)
* [MariaDB MaxScale 2.1.11 Release Notes](Release-Notes/MaxScale-2.1.11-Release-Notes.md)
* [MariaDB MaxScale 2.1.10 Release Notes](Release-Notes/MaxScale-2.1.10-Release-Notes.md)
* [MariaDB MaxScale 2.1.9 Release Notes](Release-Notes/MaxScale-2.1.9-Release-Notes.md)
* [MariaDB MaxScale 2.1.8 Release Notes](Release-Notes/MaxScale-2.1.8-Release-Notes.md)
* [MariaDB MaxScale 2.1.7 Release Notes](Release-Notes/MaxScale-2.1.7-Release-Notes.md)
* [MariaDB MaxScale 2.1.6 Release Notes](Release-Notes/MaxScale-2.1.6-Release-Notes.md)
* [MariaDB MaxScale 2.1.5 Release Notes](Release-Notes/MaxScale-2.1.5-Release-Notes.md)
* [MariaDB MaxScale 2.1.4 Release Notes](Release-Notes/MaxScale-2.1.4-Release-Notes.md)
* [MariaDB MaxScale 2.1.3 Release Notes](Release-Notes/MaxScale-2.1.3-Release-Notes.md)
* [MariaDB MaxScale 2.1.2 Release Notes](Release-Notes/MaxScale-2.1.2-Release-Notes.md)
* [MariaDB MaxScale 2.1.1 Release Notes](Release-Notes/MaxScale-2.1.1-Release-Notes.md)
* [MariaDB MaxScale 2.1.0 Release Notes](Release-Notes/MaxScale-2.1.0-Release-Notes.md)

## MariaDB MaxScale 2.0
* MariaDB MaxScale is licensed under MariaDB BSL.
* SSL can be used in the communication between MariaDB MaxScale and the backend servers.
* The number of allowed connections can explicitly be throttled.
* MariaDB MaxScale can continue serving read request even if the master has gone down.
* The security of MaxAdmin has been improved; Unix domain sockets can be used in the
  communication with MariaDB MaxScale and the Linux identity can be used for authorization.
* MariaDB MaxScale can in real time make binlog events available as raw AVRO or
  as JSON objects (beta level functionality).

For more details, please refer to:
* [MariaDB MaxScale 2.0.6 Release Notes](Release-Notes/MaxScale-2.0.6-Release-Notes.md)
* [MariaDB MaxScale 2.0.5 Release Notes](Release-Notes/MaxScale-2.0.5-Release-Notes.md)
* [MariaDB MaxScale 2.0.4 Release Notes](Release-Notes/MaxScale-2.0.4-Release-Notes.md)
* [MariaDB MaxScale 2.0.3 Release Notes](Release-Notes/MaxScale-2.0.3-Release-Notes.md)
* [MariaDB MaxScale 2.0.2 Release Notes](Release-Notes/MaxScale-2.0.2-Release-Notes.md)
* [MariaDB MaxScale 2.0.1 Release Notes](Release-Notes/MaxScale-2.0.1-Release-Notes.md)
* [MariaDB MaxScale 2.0.0 Release Notes](Release-Notes/MaxScale-2.0.0-Release-Notes.md)

## MariaDB MaxScale 1.4
* Authentication now allows table level resolution of grants. MaxScale service
  users will now need SELECT privileges on `mysql.tables_priv` to be able to
  authenticate users at the database and table level.
* Firewall filter allows whitelisting.
* Client side SSL works.

For more details, please refer to
* [MariaDB MaxScale 1.4.3 Release Notes](Release-Notes/MaxScale-1.4.3-Release-Notes.md)
* [MariaDB MaxScale 1.4.2 Release Notes](Release-Notes/MaxScale-1.4.2-Release-Notes.md)
* [MariaDB MaxScale 1.4.1 Release Notes](Release-Notes/MaxScale-1.4.1-Release-Notes.md)
* [MariaDB MaxScale 1.4.0 Release Notes](Release-Notes/MaxScale-1.4.0-Release-Notes.md).

## MariaDB MaxScale 1.3
* Added support for persistent backend connections
* The *binlog server* is now an integral component of MariaDB MaxScale.
* The logging has been changed; instead of different log files there is one log file and different message priorities.

For more details, please refer to [MariaDB MaxScale 1.3 Release Notes](Release-Notes/MaxScale-1.3.0-Release-Notes.md)

## MariaDB MaxScale 1.2
* Logfiles have been renamed. The log names are now named error.log, messages.log, trace.log and debug.log.

## MariaDB MaxScale 1.1.1

* Schemarouter now also allows for an upper limit to session commands.
* Schemarouter correctly handles SHOW DATABASES responses that span multiple buffers.
* Readwritesplit and Schemarouter now allow disabling of the session command history.

## MariaDB MaxScale 1.1

**NOTE:** MariaDB MaxScale default installation directory has changed to `/usr/local/mariadb-maxscale` and the default password for MaxAdmin is now ´mariadb´.

* New modules added
      * Binlog router
      * Firewall filter
      * Multi-Master monitor
      * RabbitMQ logging filter
      * Schema Sharding router
* Added option to use high precision timestamps in logging.
* Readwritesplit router now returns the master server's response.
* New readwritesplit router option added. It is now possible to control the amount of memory readwritesplit sessions will consume by limiting the amount of session modifying statements they can execute.
* Minimum required CMake version is now 2.8.12 for package building.
* Session idle timeout added for services. More details can be found in the configuration guide.
* Monitor API is updated to 2.0.0. Monitors with earlier versions of the API no longer work with this version of MariaDB MaxScale.
* MariaDB MaxScale now requires libcurl and libcurl development headers.
* Nagios plugins added.
* Notification service added.
* Readconnrouter has a new "running" router_option. This allows it to use any running server as a valid backend server.
* Database names can be stripped of escape characters with the `strip_db_esc` service parameter.
