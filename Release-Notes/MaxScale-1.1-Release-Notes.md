# MariaDB MaxScale 1.1 Release Notes

## 1.1 GA

This document details the changes in version 1.1 since the release of the 1.0.5 GA Release of the MaxScale product.

## New Features

### High Performance Binlog Relay
Replicate Binlog from the master to slave through MaxScale as simplified relay server for reduced network load and disaster recovery

### Database Firewall Filter
Block queries based on columns in the query, where condition, query type(select, insert, delete, update), presence of wildcard in column selection, regular expression match and time of the query

### Schema Sharding Router
Route to databases sharded by schema without application level knowledge of shard configuration

### Hint based routing
Pass hints in the SQL statement to influence the routing decision based on replication lag or time out

### Named Server Routing
Routing to a named server if incoming query matches a regular expression 

### Canonical Query logging
Convert incoming queries to canonical form and push the query and response into RabbitMQ Broker for a RabbitMQ Client to later retrieve from

### Nagios Plugin
Plugin scripts for monitoring MaxScale status and performance from a Nagios Server

### Notification Service
Receive notification of security update and patches tailored to your MaxScale configuration 

### MySQL NDB cluster support
Connection based routing to MySQL NDB clusters

### Updated installation path
MaxScale is now installed into `/usr/local/mariadb-maxscale`

## Bug Fixes

A number of bug fixes have been applied between the 1.0.5 GA and this GA release. The table below lists the bugs that have been resolved. The details for each of these may be found in https://mariadb.atlassian.net/projects/MXS or in the former http://bugs.mariadb.com Bug database

<table>
  <tr>
    <td>ID</td>
    <td>Summary</td>
  </tr>
  <tr>
    <td>MXS-80</td>
    <td>"show sessions" can crash MaxScale</td>
  </tr>
  <tr>
    <td>MXS-79</td>
    <td>schemarouter hangs if client connects with empty database</td>
  </tr>
  <tr>
    <td>MXS-78</td>
    <td>"USE" statement gives unpredictable/unexpected results</td>
  </tr>
  <tr>
    <td>MXS-76</td>
    <td>core/dbusers.c needs better error messages</td>
  </tr>
  <tr>
    <td>MXS-74</td>
    <td>Crash when no arguments given to on_queries clause</td>
  </tr>
  <tr>
    <td>MXS-72</td>
    <td>dbfwfilter on_queries clause appears to be ignored</td>
  </tr>
  <tr>
    <td>MXS-71</td>
    <td>dbfwfilter at_times clause seems to erroneously block user</td>
  </tr>
  <tr>
    <td>MXS-68</td>
    <td>Wrong rule name in dbfwfilter leads to MaxScale crash</td>
  </tr>
  <tr>
    <td>MXS-65</td>
    <td>Omitting <any|all|strict_all> in users directive causes crash in libdbfwfilter.so(link_rules)</td>
  </tr>
  <tr>
    <td>MXS-63</td>
    <td>Maxkeys and Maxpasswd log to /tpm</td>
  </tr>
  <tr>
    <td>MXS-57</td>
    <td>MaxScale should write a message to the error log when config is not found</td>
  </tr>
  <tr>
    <td>MXS-54</td>
    <td>Write failed auth attempt to trace log</td>
  </tr>
  <tr>
    <td>MXS-50</td>
    <td>Removing 1.0.5 RPM gives error about /etc/ld.so.conf.d/maxscale.conf</td>
  </tr>
  <tr>
    <td>MXS-47</td>
    <td>Session freeze when small tail packet</td>
  </tr>
  <tr>
    <td>MXS-5</td>
    <td>Possible memory leak in readwritesplit router</td>
  </tr>
  <tr>
    <td>736</td>
    <td>Memory leak while doing read/write splitting</td>
  </tr>
  <tr>
    <td>733</td>
    <td>Init-script deletes bin/maxscale</td>
  </tr>
  <tr>
    <td>732</td>
    <td>Build is broken: CentOS/RHEL 5 and SLES 11</td>
  </tr>
  <tr>
    <td>730</td>
    <td>Regex filter and shorter than original replacement queries MaxScale</td>
  </tr>
  <tr>
    <td>729</td>
    <td>PDO prepared statements bug introduced in Maxscale 1.0.5</td>
  </tr>
  <tr>
    <td>721</td>
    <td>Documentation suggests SIGTERM to re-read config file</td>
  </tr>
  <tr>
    <td>716</td>
    <td>$this->getReadConnection()->query('SET @id = 0;');</td>
  </tr>
  <tr>
    <td>709</td>
    <td>"COPYRIGHT LICENSE README SETUP" files go to /usr/local/mariadb-maxscale/ after 'make package'</td>
  </tr>
  <tr>
    <td>704</td>
    <td>"make testall" returns success status (exit code 0) even on failures</td>
  </tr>
  <tr>
    <td>698</td>
    <td>Using invalid parameter in many maxadmin commands causes MaxScale to fail</td>
  </tr>
  <tr>
    <td>693</td>
    <td>Freeing tee filter's orphaned sessions causes a segfault when embedded server closes</td>
  </tr>
  <tr>
    <td>690</td>
    <td>CPU/architecture is hardcoded into debian/rules</td>
  </tr>
  <tr>
    <td>686</td>
    <td>TestService fails because of the modules used in it aren't meant for actual use</td>
  </tr>
  <tr>
    <td>677</td>
    <td>Race condition in tee filter clientReply</td>
  </tr>
  <tr>
    <td>676</td>
    <td>"Write to backend failed. Session closed." when changing default database via readwritesplit with max_slave_connections != 100%</td>
  </tr>
  <tr>
    <td>673</td>
    <td>MaxScale crashes if "Users table data" is empty and "show dbusers" is executed in maxadmin</td>
  </tr>
  <tr>
    <td>670</td>
    <td>Tee filter: statement router loses statements when other router gets enough ahead</td>
  </tr>
  <tr>
    <td>665</td>
    <td>Core: accessing freed memory when session is closed</td>
  </tr>
  <tr>
    <td>659</td>
    <td>MaxScale doesn't shutdown if none of the configured services start</td>
  </tr>
  <tr>
    <td>648</td>
    <td>use database is sent forever with tee filter to a readwrite split service</td>
  </tr>
  <tr>
    <td>620</td>
    <td>enable_root_user=true generates errors to error log</td>
  </tr>
  <tr>
    <td>612</td>
    <td>Service was started although no users could be loaded from database</td>
  </tr>
  <tr>
    <td>600</td>
    <td>RWSplit: if session command fails in some backend, it is not dropped from routing session</td>
  </tr>
  <tr>
    <td>587</td>
    <td>Hint filter don't work if listed before regex filter in configuration file</td>
  </tr>
  <tr>
    <td>579</td>
    <td>serviceStartProtocol test crashes</td>
  </tr>
  <tr>
    <td>506</td>
    <td>Don't write to shm/tmpfs by default without telling and without a way to override it</td>
  </tr>
  <tr>
    <td>503</td>
    <td>TOC in the bundled PDFs doesn't link to actual sections</td>
  </tr>
  <tr>
    <td>457</td>
    <td>Please provide a list of build dependencies for building MaxScale</td>
  </tr>
  <tr>
    <td>361</td>
    <td>file_exists() *modifies* the file it checks for???</td>
  </tr>
  <tr>
    <td>338</td>
    <td>Log manager spread down feature is disabled</td>
  </tr>
  <tr>
    <td>159</td>
    <td>Memory leak. Dbusers are loaded into memory but not unloaded</td>
  </tr>
</table>


## Known Issues

There are a number bugs and known limitations within this version of MaxScale, the most serious of this are listed below.

* The Read/Write Splitter is a little too strict when it receives errors from slave servers during execution of session commands. This can result in sessions being terminated in situation in which MaxScale could recover without terminating the sessions.

* MaxScale can not manage authentication that uses wildcard matching in hostnames in the mysql.user table of the backend database. The only wildcards that can be used are in IP address entries.

* When users have different passwords based on the host from which they connect MaxScale is unable to determine which password it should use to connect to the backend database. This results in failed connections and unusable usernames in MaxScale.

* Service init script is missing after upgrade from 1.0 in RPM-based system. Can be fixed by reinstalling the package ('yum reinstall maxscale' or 'rpm -i --force /maxscale-1.1.rpm')

* Binlog Router Plugin is compatible with MySQL 5.6  
  Binlog Router Plugin currently does not work for MariaDB 5.5 and MariaDB 10.0 

* LONGBLOG are currently not supported.

* Galera Cluster variables, such as @@wsrep_node_name, are not resolved by the embedded MariaDB parser.

* The Database Firewall filter does not support multi-statements. Using them will result in an error being sent to the client.   

## Packaging

Both RPM and Debian packages are available for MaxScale in addition to the tar based releases previously distributed we now provide

* CentOS/RedHat 5

* CentOS/RedHat 6

* CentOS/RedHat 7

* Debian 6

* Debian 7

* Ubuntu 12.04 LTS

* Ubuntu 13.10

* Ubuntu 14.04 LTS

* Fedora 19

* Fedora 20

* OpenSuSE 13

* SuSE Linux Enterprise 11

* SuSE Linux Enterprise 12
