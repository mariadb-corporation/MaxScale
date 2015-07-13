# MariaDB MaxScale 1.0.4 Release Notes

1.0.4 GA

This document details the changes in version 1.0.4 since the release of the 1.0.2 Release Candidate of the MaxScale product.

## New Features

No new features have been introduced since the released candidate was released.

## Bug Fixes

A number of bug fixes have been applied between the 0.6 alpha and this alpha release. The table below lists the bugs that have been resolved. The details for each of these may be found in bugs.mariadb.com.

<table>
  <tr>
    <td>ID</td>
    <td>Summary</td>
  </tr>
  <tr>
    <td>644</td>
    <td>Buffered that were cloned using the gwbuf_clone routine failed to initialise the buffer lock structure correctly.</td>
  </tr>
  <tr>
    <td>643</td>
    <td>Recursive filter definitions in the configuration file could cause MaxScale to loop</td>
  </tr>
  <tr>
    <td>665</td>
    <td>An access to memory that had already been freed could be made within the MaxScale core</td>
  </tr>
  <tr>
    <td>664</td>
    <td>MySQL Authentication code could access memory that had already been freed.</td>
  </tr>
  <tr>
    <td>673</td>
    <td>MaxScale could crash if it had an empty user table and the MaxAdmin show dbusers command was run</td>
  </tr>
  <tr>
    <td>670</td>
    <td>The tee filter could lose statement on the branch service if the branch service was significantly slower at executing statements compared with the main service.</td>
  </tr>
  <tr>
    <td>653</td>
    <td>Memory corruption could occur with extremely long hostnames in the mysql.user table.</td>
  </tr>
  <tr>
    <td>657</td>
    <td>If the branch service of a tee filter shutdown unexpectedly then MaxScale could fail</td>
  </tr>
  <tr>
    <td>654</td>
    <td>Missing quotes in MaxAdmin show dbusers command could cause MaxAdmin to crash</td>
  </tr>
  <tr>
    <td>677</td>
    <td>A race condition existed in the tee filter client reply handling</td>
  </tr>
  <tr>
    <td>658</td>
    <td>The readconnroute router did not correctly close sessions when a backend database failed</td>
  </tr>
  <tr>
    <td>662</td>
    <td>MaxScale startup hangs if no backend servers respond</td>
  </tr>
  <tr>
    <td>676</td>
    <td>MaxScale writes a log entry, "Write to backend failed. Session closed." when changing default database via readwritesplit with max_slave_connections != 100%</td>
  </tr>
  <tr>
    <td>650</td>
    <td>Tee filter does not correctly detect missing branch service</td>
  </tr>
  <tr>
    <td>645</td>
    <td>Tee filter can hang MaxScale if the read/write splitter is used</td>
  </tr>
  <tr>
    <td>678</td>
    <td>Tee filter does not always send full query to branch service</td>
  </tr>
  <tr>
    <td>679</td>
    <td>A shared pointer in the service was leading to misleading service states</td>
  </tr>
  <tr>
    <td>680</td>
    <td>The Read/Write Splitter can not load users if there are no databases available at startup</td>
  </tr>
  <tr>
    <td>681</td>
    <td>The Read/Write Splitter could crash is the value of max_slave_connections was set to a low percentage and only a small number of backend servers are available</td>
  </tr>
</table>


## Known Issues

There are a number bugs and known limitations within this version of MaxScale, the most serious of this are listed below.

* The SQL construct "LOAD DATA LOCAL INFILE" is not fully supported.

* The Read/Write Splitter is a little too strict when it receives errors from slave servers during execution of session commands. This can result in sessions being terminated in situation in which MaxScale could recover without terminating the sessions.

* MaxScale can not manage authentication that uses wildcard matching in hostnames in the mysql.user table of the backend database. The only wildcards that can be used are in IP address entries.

* When users have different passwords based on the host from which they connect MaxScale is unable to determine which password it should use to connect to the backend database. This results in failed connections and unusable usernames in MaxScale.

# Packaging

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

# MaxScale Home Default Value

The installation assumes that the default value for the environment variable MAXSCALE_HOME is set to /usr/local/mariadb/maxscale. This is hard coded in the service startup file that is placed in /etc/init.d/maxscale by the installation process.

