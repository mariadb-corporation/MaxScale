#Changelog

## MariaDB MaxScale 2.0
* MariaDB MaxScale is licensed under MariaDB BSL.
* SSL can be used in the communication between MariaDB MaxScale and the backend servers.
* The number of allowed connections can explicitly be throttled.
* MariaDB MaxScale can continue serving read request even if the master has gone down.
* The security of MaxAdmin has been improved; you can only connect from the
  same host MariaDB MaxScale is running on, and the Linux identity is used for
  authorization.
* MariaDB MaxScale can in real time make binlog events available as raw AVRO or
  as JSON objects (beta level functionality).

For more details, please refer to:
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
