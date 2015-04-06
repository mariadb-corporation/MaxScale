#Changelog

These are the changes introduced in the next MaxScale version. This is not the official change log and the latest changelog can always be found in here: [MaxScale 1.1 Release Notes](Release-Notes/MaxScale-1.1-Release-Notes.md)

**NOTE:** MaxScale default installation directory has changed to `/usr/local/mariadb-maxscale` and the default password for MaxAdmin is now ´mariadb´.

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
* Monitor API is updated to 2.0.0. Monitors with earlier versions of the API no longer work with this version of MaxScale.
* MaxScale now requires libcurl and libcurl development headers.
* Nagios plugins added.
* Notification service added.
* Readconnrouter has a new "running" router_option. This allows it to use any running server as a valid backend server.
* Database names can be stripped of escape characters with the `strip_db_esc` service parameter.
