
# Contents

## About MariaDB MaxScale

 - [About MariaDB MaxScale](About/About-MaxScale.md)
 - [Changelog](Changelog.md)
 - [Limitations](About/Limitations.md)

## Getting Started

 - [MariaDB MaxScale Installation Guide](Getting-Started/MariaDB-MaxScale-Installation-Guide.md)
 - [Building MariaDB MaxScale from Source Code](Getting-Started/Building-MaxScale-from-Source-Code.md)
 - [Configuration Guide](Getting-Started/Configuration-Guide.md)
 - [MaxGUI](Getting-Started/MaxGUI.md)

## Upgrading MariaDB MaxScale

- [Upgrading MariaDB MaxScale from 2.5 to 6](Upgrading/Upgrading-To-MaxScale-6.md)
- [Upgrading MariaDB MaxScale from 2.4 to 2.5](Upgrading/Upgrading-To-MaxScale-2.5.md)
- [Upgrading MariaDB MaxScale from 2.3 to 2.4](Upgrading/Upgrading-To-MaxScale-2.4.md)
- [Upgrading MariaDB MaxScale from 2.2 to 2.3](Upgrading/Upgrading-To-MaxScale-2.3.md)
- [Upgrading MariaDB MaxScale from 2.1 to 2.2](Upgrading/Upgrading-To-MaxScale-2.2.md)
- [Upgrading MariaDB MaxScale from 2.0 to 2.1](Upgrading/Upgrading-To-MaxScale-2.1.md)
- [Upgrading MariaDB MaxScale from 1.4 to 2.0](Upgrading/Upgrading-To-MaxScale-2.0.md)

## Reference

 - [MaxCtrl - Command Line Admin Interface](Reference/MaxCtrl.md)
 - [MaxScale REST API](REST-API/API.md)
 - [Module Commands](Reference/Module-Commands.md)
 - [Routing Hints](Reference/Hint-Syntax.md)

## Tutorials

The main tutorial for MariaDB MaxScale consist of setting up MariaDB MaxScale for the environment you are using with either a connection-based or a read/write-based configuration.

 - [MariaDB MaxScale Tutorial](Tutorials/MaxScale-Tutorial.md)

These tutorials are for specific use cases and module combinations.

 - [Administration Tutorial](Tutorials/Administration-Tutorial.md)
 - [Avro Router Tutorial](Tutorials/Avrorouter-Tutorial.md)
 - [Connection Routing Tutorial](Tutorials/Connection-Routing-Tutorial.md)
 - [Filter Tutorial](Tutorials/Filter-Tutorial.md)
 - [MariaDB Monitor Failover Tutorial](Tutorials/MariaDB-Monitor-Failover.md)
 - [Read Write Splitting Tutorial](Tutorials/Read-Write-Splitting-Tutorial.md)
 - [Simple Schema Sharding Tutorial](Tutorials/Simple-Sharding-Tutorial.md)
 - [Xpand Monitor Tutorial](Tutorials/Configuring-Xpand-Monitor.md)
 - [Xpand Usage Tutorial](Tutorials/MaxScale-Xpand-Tutorial.md)

Here are tutorials on monitoring and managing MariaDB MaxScale in cluster environments.

 - [REST API Tutorial](Tutorials/REST-API-Tutorial.md)

## Routers

The routing module is the core of a MariaDB MaxScale service. The router documentation
contains all module specific configuration options and detailed explanations
of their use.

 - [Avrorouter](Routers/Avrorouter.md)
 - [Binlogrouter](Routers/Binlogrouter.md)
 - [Cat](Routers/Cat.md)
 - [HintRouter](Routers/HintRouter.md)
 - [KafkaCDC](Routers/KafkaCDC.md)
 - [MirrorRouter](Routers/Mirror.md)
 - [Read Connection Router](Routers/ReadConnRoute.md)
 - [Read Write Split](Routers/ReadWriteSplit.md)
 - [Schemarouter](Routers/SchemaRouter.md)
 - [SmartRouter](Routers/SmartRouter.md)

## Filters

Here are detailed documents about the filters MariaDB MaxScale offers. They contain configuration guides and example use cases. Before reading these, you should have read the filter tutorial so that you know how they work and how to configure them.

 - [Binlog Filter](Filters/BinlogFilter.md)
 - [Cache](Filters/Cache.md)
 - [Consistent Critical Read Filter](Filters/CCRFilter.md)
 - [Database Firewall Filter](Filters/Database-Firewall-Filter.md)
 - [Hint Filter](Filters/Hintfilter.md)
 - [Insert Stream Filter](Filters/Insert-Stream-Filter.md)
 - [Luafilter](Filters/Luafilter.md)
 - [Masking Filter](Filters/Masking.md)
 - [Maxrows Filter](Filters/Maxrows.md)
 - [Named Server Filter](Filters/Named-Server-Filter.md)
 - [Query Log All](Filters/Query-Log-All-Filter.md)
 - [Regex Filter](Filters/Regex-Filter.md)
 - [Tee Filter](Filters/Tee-Filter.md)
 - [Throttle Filter](Filters/Throttle.md)
 - [Top N Filter](Filters/Top-N-Filter.md)
 - [Transaction Performance Monitoring Filter](Filters/Transaction-Performance-Monitoring-Filter.md)

## Monitors

Common options for all monitor modules.

 - [Monitor Common](Monitors/Monitor-Common.md)

Module specific documentation.

 - [Aurora Monitor](Monitors/Aurora-Monitor.md)
 - [ColumnStore Monitor](Monitors/ColumnStore-Monitor.md)
 - [Galera Monitor](Monitors/Galera-Monitor.md)
 - [MariaDB Monitor](Monitors/MariaDB-Monitor.md)
 - [Xpand Monitor](Monitors/Xpand-Monitor.md)

## Protocols

Documentation for MaxScale protocol modules.

 - [Change Data Capture (CDC) Protocol](Protocols/CDC.md)
 - [Change Data Capture (CDC) Users](Protocols/CDC_users.md)
 - [NoSQL](Protocols/NoSQL.md)

The MaxScale CDC Connector provides a C++ API for consuming data from a CDC system.

 - [CDC Connector](Connectors/CDC-Connector.md)

## Authenticators

A short description of the authentication module type can be found in the
[Authentication Modules](Authenticators/Authentication-Modules.md)
document.

 - [MySQL Authenticator](Authenticators/MySQL-Authenticator.md)
 - [GSSAPI Authenticator](Authenticators/GSSAPI-Authenticator.md)

## Design Documents

 - [Plugin development guide](Design-Documents/Plugin-development-guide.md)
