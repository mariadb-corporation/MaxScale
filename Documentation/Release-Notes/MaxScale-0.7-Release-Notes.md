# MariaDB MaxScale 0.7 Alpha Release Notes

0.7 Alpha

This document details the changes in version 0.7 since the release of the 0.6 alpha of the MaxScale product.

# New Features

## Galera Support

Enhanced support for Galera cluster to allow Galera to be used as a High Available Cluster with no write contention between the nodes..

MaxScale will control access to a Galera Cluster such that one node is designated as the master node to which all write operations will be sent. Read operations will be sent to any of the remaining nodes that are part of the cluster. Should the currently elected master node fail MaxScale will automatically promote one of the remaining nodes to become the new master node.

## Multiple Slave Connections

The Read/Write Split query router has been enhanced to allow multiple slaves connections to be created. The number of slave connections is configurable via a parameter in the MaxScale configuration file.

Adding multiple connections allows for better load balancing between the slaves and in a pre-requisite for providing improved fault tolerance within the Read/Write Splitter. The selection of which slave to use for a particular read operation can be controlled via options in the router configuration.

## Debug Interface Enhancements

A number of new list commands have been added to the debug interface to allow more concise tabular output of certain object types within the interface.

**MaxScale>** help list

Available options to the list command:

    filters    List all the filters defined within MaxScale

    listeners  List all the listeners defined within MaxScale

    modules    Show all currently loaded modules

    services   List all the services defined within MaxScale

    servers    List all the servers defined within MaxScale

    sessions   List all the active sessions within MaxScale

**MaxScale>** 

Those objects that are defined in the configuration file can now be referenced by the names used in the configuration file rather than by using memory addresses. This means that services, servers, monitors and filters can all now be referenced using meaningful names provided by the user. Internal objects such as DCB’s and sessions, which are not named in the configuration file still require the use of memory addresses.

Two modes of operation of the interface are now available, user mode and developer mode. The user mode restricts access to the feature that allow arbitrary structures to be examined and checks all memory address for validity before allowing access. 

## Maintenance Mode for Servers

MaxScale now provides a maintenance mode for servers, this mode allows servers to be set such that no new connections will be opened to that server. Also, servers in maintenance mode are not monitored by MaxScale. This allows an administrator to set a server into maintenance mode when it is required to be taken out of use. The connections will then diminish over time and since no new connections are created, the administrator can remove the node from use to perform some maintenance activities.

Nodes are placed into maintenance mode via the debug interface using the set server command.

**MaxScale>** set server datanode3 maintenance

Nodes are taken out of maintenance using the clear server command.

**MaxScale>** clear server datanode3 maintenance 

## Configurable Monitoring Interval

All monitor plugins now provide a configuration parameter that can be set to control how frequently the MaxScale monitoring is performed.

## Replication Lag Heartbeat Monitor

The mysqlmon monitor module now implements a replication heartbeat protocol that is used to determine the lag between updates to the master and those updates being applied to the slave. This information is then made available to routing modules and may be used to determine if a particular slave node may be used or which slave node is most up to date.

## Filters API

The first phase of the filter API is available as part of this release. This provides filtering for the statements from the client application to the router. Filtering for the returned results has not yet been implemented and will be available in a future version.

Three example filters are including in the release

1. Statement counting Filter - a simple filter that counts the number of SQL statements executed within a session. Results may be viewed via the debug interface.

2. Query Logging Filter - a simple query logging filter that write all statements for a session into a log file for that session.

3. Query Rewrite Filter - an example of how filters can alter the query contents. This filter allows a regular expression to be defined, along with replacement text that should be substituted for every match of that regular expression.

## MariaDB 10 Replication Support

The myqlmon monitor module has been updated to support the new syntax for show all slaves status in MariaDB in order to correctly determine the master and slave state of each server being monitor. Determination of MariaDB 10 is automatically performed by the monitor and no configuration is required.

## API Versioning

The module interface has been enhanced to allow the API version in use to be reported, along with the status of the module and a short description of the module. The status allows for differentiation of the release status of a plugin to be identified independently of the core of MaxScale. plugins may be designated as "in development", “alpha”, “beta” or “GA”.

**MaxScale>** list modules

Module Name     | Module Type | Version | API   | Status

----------------------------------------------------------------

regexfilter     | Filter      | V1.0.0  | 1.0.0 | Alpha

MySQLBackend    | Protocol    | V2.0.0  | 1.0.0 | Alpha

telnetd         | Protocol    | V1.0.1  | 1.0.0 | Alpha

MySQLClient     | Protocol    | V1.0.0  | 1.0.0 | Alpha

mysqlmon        | Monitor     | V1.2.0  | 1.0.0 | Alpha

readwritesplit  | Router      | V1.0.2  | 1.0.0 | Alpha

readconnroute   | Router      | V1.0.2  | 1.0.0 | Alpha

debugcli        | Router      | V1.1.1  | 1.0.0 | Alpha

**MaxScale>** 

# Bug Fixes

A number of bug fixes have been applied between the 0.6 alpha and this alpha release. The table below lists the bugs that have been resolved. The details for each of these may be found in bugs.skysql.com.

<table>
  <tr>
    <td>ID</td>
    <td>Summary</td>
  </tr>
  <tr>
    <td>443</td>
    <td>mysql/galera monitors hang when backend fails</td>
  </tr>
  <tr>
    <td>424</td>
    <td>Read/Write Splitter closes connection without sending COM_QUIT</td>
  </tr>
  <tr>
    <td>438</td>
    <td>Internal thread deadlock</td>
  </tr>
  <tr>
    <td>436</td>
    <td>Sessions in invalid state</td>
  </tr>
  <tr>
    <td>359</td>
    <td>Router options for Read/Write Split module</td>
  </tr>
  <tr>
    <td>435</td>
    <td>Some automated tests have invalid SQL syntax</td>
  </tr>
  <tr>
    <td>431</td>
    <td>rwsplit.sh test script has incorrect bash syntax</td>
  </tr>
  <tr>
    <td>425</td>
    <td>MaxScale crashes after prolonged use</td>
  </tr>
</table>


# Linking

Following reported issues with incompatibilities between MaxScale and the shared library used by MySQL this version of MaxScale will be statically linked with the MariaDB 5.5 embedded library that it requires. This library is used for internal purposes only and does not result in MaxScale support for other versions of MySQL or MariaDB being affected.

