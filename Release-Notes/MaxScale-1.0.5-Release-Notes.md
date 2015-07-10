# MariaDB MaxScale 1.0.5 Release Notes

This document details the changes in version 1.0.5 since the release of the 1.0.4 GA of the MaxScale product.

# New Features
No new features have been introduced since the GA version was released. SuSE Enterprise 11 and 12 packages are now also supplied.

# Bug Fixes

A number of bug fixes have been applied between the 1.0.4 initial GA release and this GA release. The table below lists the bugs that have been resolved. The details for each of these may be found in bugs.mariadb.com.

<table>
   <tr>
   <td>ID</td>
   <td>Summary</td>
   </tr>
<tr>
   <td>519</td>
   <td>LOAD DATA LOCAL INFILE not handled?</td>
</tr>
<tr>
   <td>714</td>
   <td>Error log flooded when too many connect errors causes the MaxScale host to be blocked</td>
</tr>
<tr>
   <td>711</td>
   <td>Some MySQL Workbench Management actions hang with R/W split router</td>
</tr>
<tr>
   <td>710</td>
   <td>make package install files in /etc/init.d</td>
</tr>
<tr>
   <td>683</td>
   <td>Check for unsupported version of MariaDB</td>
</tr>
<tr>
   <td>684</td>
   <td>Use mysql_config to determine include/lib directory paths and compiler options</td>
</tr>
<tr>
   <td>689</td>
   <td>cmake ­DCMAKE_INSTALL_PREFIX has no effect</td>
</tr>
<tr>
   <td>701</td>
   <td>set server <svr> maint fails on the command line</td>
</tr>
<tr>
   <td>705</td>
   <td>Authentication fails when the user connects to a database with the SQL mode including ANSI_QUOTES</td>
</tr>
<tr>
   <td>507</td>
   <td>R/W split does not send last_insert_id() to the master</td>
</tr>
<tr>
   <td>700</td>
   <td>maxscale ­­version has no output</td>
</tr>
<tr>
   <td>694</td>
   <td>RWSplit SELECT @a:=@a+1 as a, test.b from test breaks client session</td>
</tr>
<tr>
   <td>685</td>
   <td>SELECT against readconnrouter fails when large volumes of data are returned and the tee filter is used</td>
</tr>
</table>

# Known Issues

There are a number bugs and known limitations within this version of MaxScale, the most serious of this are listed below.

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

* SuSE Enterprise 11

* SuSE Enterprise 12

# MaxScale Home Default Value

The installation assumes that the default value for the environment variable MAXSCALE_HOME is set to /usr/local/skysql/maxscale. This is hard coded in the service startup file that is placed in /etc/init.d/maxscale by the installation process.
