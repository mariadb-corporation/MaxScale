
[Search page for MariaDB MaxScale Documentation](http://mariadb-corporation.github.io/MaxScale/Search/)

# Contents

## About MariaDB MaxScale

 - [About MariaDB MaxScale](About/About-MaxScale.md)
 - [Release Notes](Release-Notes/MaxScale-2.0.0-Release-Notes.md)
 - [Changelog](Changelog.md)
 - [Limitations](About/Limitations.md)
 - [COPYRIGHT](About/COPYRIGHT.md)
 - [LICENSE](About/LICENSE.md)

## Getting Started

 - [MariaDB MaxScale Installation Guide](Getting-Started/MariaDB-MaxScale-Installation-Guide.md)
 - [Building MariaDB MaxScale from Source Code](Getting-Started/Building-MaxScale-from-Source-Code.md)
 - [Configuration Guide](Getting-Started/Configuration-Guide.md)

## Upgrading MariaDB MaxScale

- [Upgrading MariaDB MaxScale from 1.4 to 2.0](Upgrading/Upgrading-To-MaxScale-2.0.md)
- [Upgrading MariaDB MaxScale from 1.3 to 1.4](Upgrading/Upgrading-To-MaxScale-1.4.md)
- [Upgrading MariaDB MaxScale from 1.2 to 1.3](Upgrading/Upgrading-To-MaxScale-1.3.md)
- [Upgrading MariaDB MaxScale from 1.1.1 to 1.2](Upgrading/Upgrading-To-MaxScale-1.2.md)
- [Upgrading MariaDB MaxScale from 1.0.5 to 1.1.0](Upgrading/Upgrading-To-MaxScale-1.1.0.md)

## Reference

 - [MaxAdmin](Reference/MaxAdmin.md)
 - [How Errors are Handled in MariaDB MaxScale](Reference/How-errors-are-handled-in-MaxScale.md)
 - [Debug and Diagnostic Support](Reference/Debug-And-Diagnostic-Support.md)
 - [Routing Hints](Reference/Hint-Syntax.md)
 - [MaxBinlogCheck](Reference/MaxBinlogCheck.md)

## Tutorials

The main tutorial for MariaDB MaxScale consist of setting up MariaDB MaxScale for the environment you are using with either a connection-based or a read/write-based configuration.

 - [MariaDB MaxScale Tutorial](Tutorials/MaxScale-Tutorial.md)

These tutorials are for specific use cases and module combinations.

 - [Administration Tutorial](Tutorials/Administration-Tutorial.md)
 - [Filter Tutorial](Tutorials/Filter-Tutorial.md)
 - [MariaDB MaxScale Information Schema Tutorial](Tutorials/MaxScale-Information-Schema.md)
 - [MySQL Cluster Setup](Tutorials/MySQL-Cluster-Setup.md)
 - [Replication Proxy with the Binlog Router Tutorial](Tutorials/Replication-Proxy-Binlog-Router-Tutorial.md)
 - [RabbitMQ Setup and MariaDB MaxScale Integration Tutorial](Tutorials/RabbitMQ-Setup-And-MaxScale-Integration.md)
 - [RabbitMQ and Tee Filter Data Archiving Tutorial](Tutorials/RabbitMQ-And-Tee-Archiving.md)
 - [Simple Schema Sharding Tutorial](Tutorials/Simple-Sharding-Tutorial.md)

Here are tutorials on monitoring and managing MariaDB MaxScale in cluster environments.

 - [Nagios Plugins for MariaDB MaxScale Tutorial](Tutorials/Nagios-Plugins.md)
 - [MariaDB MaxScale HA with Corosync-Pacemaker](Tutorials/MaxScale-HA-with-Corosync-Pacemaker.md)
 - [MariaDB MaxScale HA with Lsyncd](Tutorials/MaxScale-HA-with-lsyncd.md)

## Routers

The routing module is the core of a MariaDB MaxScale service. The router documentation
contains all module specific configuration options and detailed explanations
of their use.

 - [Read Write Split](Routers/ReadWriteSplit.md)
 - [Read Connection Router](Routers/ReadConnRoute.md)
 - [Schemarouter](Routers/SchemaRouter.md)
 - [Binlogrouter](Routers/Binlogrouter.md)
 - [Avrorouter](Routers/Avrorouter.md)

There are also two diagnostic routing modules. The CLI is for MaxAdmin and
the Debug CLI client for Telnet.

 - [CLI](Routers/CLI.md)
 - [Debug CLI](Routers/Debug-CLI.md)

## Filters

Here are detailed documents about the filters MariaDB MaxScale offers. They contain configuration guides and example use cases. Before reading these, you should have read the filter tutorial so that you know how they work and how to configure them.

 - [Query Log All](Filters/Query-Log-All-Filter.md)
 - [Regex Filter](Filters/Regex-Filter.md)
 - [Tee Filter](Filters/Tee-Filter.md)
 - [Top N Filter](Filters/Top-N-Filter.md)
 - [Database Firewall Filter](Filters/Database-Firewall-Filter.md)
 - [RabbitMQ Filter](Filters/RabbitMQ-Filter.md)
 - [Named Server Filter](Filters/Named-Server-Filter.md)

## Monitors

Common options for all monitor modules.

 - [Monitor Common](Monitors/Monitor-Common.md)

Module specific documentation.

 - [MySQL Monitor](Monitors/MySQL-Monitor.md)
 - [Galera Monitor](Monitors/Galera-Monitor.md)
 - [Multi-Master Monitor](Monitors/MM-Monitor.md)
 - [MySQL Cluster Monitor](Monitors/NDB-Cluster-Monitor.md)

## Utilities

 - [RabbitMQ Consumer Client](Filters/RabbitMQ-Consumer-Client.md)

## Design Documents

 - [Core Objects Design (in development)](http://mariadb-corporation.github.io/MaxScale/Design-Documents/core-objects-html-docs)
 - [Binlog Router Design (in development)](http://mariadb-corporation.github.io/MaxScale/Design-Documents/binlog-router-html-docs)
 - [DCB States (to be replaced in StarUML)](Design-Documents/DCB-States.pdf)
 - [Schema Sharding Router Technical Documentation](Design-Documents/SchemaRouter-technical.md)

## Earlier Release Notes

 - [MariaDB MaxScale 1.4.3 Release Notes](Release-Notes/MaxScale-1.4.3-Release-Notes.md)
 - [MariaDB MaxScale 1.4.2 Release Notes](Release-Notes/MaxScale-1.4.2-Release-Notes.md)
 - [MariaDB MaxScale 1.4.1 Release Notes](Release-Notes/MaxScale-1.4.1-Release-Notes.md)
 - [MariaDB MaxScale 1.4.0 Release Notes](Release-Notes/MaxScale-1.4.0-Release-Notes.md)
 - [MariaDB MaxScale 1.3.0 Release Notes](Release-Notes/MaxScale-1.3.0-Release-Notes.md)
 - [MariaDB MaxScale 1.2.0 Release Notes](Release-Notes/MaxScale-1.2.0-Release-Notes.md)
 - [MariaDB MaxScale 1.1.1 Release Notes](Release-Notes/MaxScale-1.1.1-Release-Notes.md)
 - [MariaDB MaxScale 1.1.0 Release Notes](Release-Notes/MaxScale-1.1-Release-Notes.md)
 - [MariaDB MaxScale 1.0.3 Release Notes](Release-Notes/MaxScale-1.0.3-Release-Notes.md)
 - [MariaDB MaxScale 1.0.1 Release Notes](Release-Notes/MaxScale-1.0.1-Release-Notes.md)
 - [MariaDB MaxScale 1.0 Release Notes](Release-Notes/MaxScale-1.0-Release-Notes.md)
 - [MariaDB MaxScale 0.7 Release Notes](Release-Notes/MaxScale-0.7-Release-Notes.md)
 - [MariaDB MaxScale 0.6 Release Notes](Release-Notes/MaxScale-0.6-Release-Notes.md)
 - [MariaDB MaxScale 0.5 Release Notes](Release-Notes/MaxScale-0.5-Release-Notes.md)

