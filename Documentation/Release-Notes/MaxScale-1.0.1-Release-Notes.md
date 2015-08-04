# MariaDB MaxScale 1.0.1 Beta Release Notes

1.0.1 Beta

This document details the changes in version 1.0.1 since the release of the 1.0 beta of the MaxScale product.

# New Features

## CMake build system

Building MaxScale is now easier than ever thanks to the introduction of CMake into the build process. Building with CMake removes the need to edit files, specify directory locations or change build flags, in all but the rarest of the cases, and building with non-standard configurations is a lot easier thanks to the easy configuration of all the build parameters.

Here’s a short list of the most common build parameters,their functions and default values.

<table>
  <tr>
    <td>Variable</td>
    <td>Purpose</td>
    <td>Default value</td>
  </tr>
  <tr>
    <td>INSTALL_DIR</td>
    <td>Root location of the MaxScale install</td>
    <td>/usr/local/skysql/maxscale</td>
  </tr>
  <tr>
    <td>STATIC_EMBEDDED</td>
    <td>Whether to use the static or the dynamic version of the embedded library</td>
    <td>No</td>
  </tr>
  <tr>
    <td>OLEVEL</td>
    <td>Level of optimization used when compiling</td>
    <td>No optimization</td>
  </tr>
  <tr>
    <td>INSTALL_SYSTEM_FILES</td>
    <td>If startup scripts should be installed into /etc/init.d and ldconfig configuration files to /etc/ld.so.conf.d</td>
    <td>Yes</td>
  </tr>
  <tr>
    <td>BUILD_TYPE</td>
    <td>The type of the build. ‘None’ for normal, ‘Debug’ for debugging and ‘Optimized’ for an optimized build.</td>
    <td>None</td>
  </tr>
</table>


Details on all the configurable parameters and instructions on how to use CMake can be found in the README file.

## Enhancements

The polling mechanism in MaxScale has been modified to overcome a flaw which mean that connections with a heavy I/O load could starve other connections within MaxScale and prevent query execution. This has been resolved with a more fairer event scheduling mechanism within the MaxScale polling subsystem. This has led to improve overall performance in high load situations.

# Bug Fixes

A number of bug fixes have been applied between the 1.0 beta release and this release candidate. The table below lists the bugs that have been resolved. The details for each of these may be found in bugs.skysql.com.

<table>
  <tr>
    <td>ID</td>
    <td>Summary</td>
  </tr>
  <tr>
    <td>462</td>
    <td>Testall target fails in server/test to invalid MAXSCALE_HOME path specification</td>
  </tr>
  <tr>
    <td>467</td>
    <td>max_slave_replication lag is not effective after session creation</td>
  </tr>
  <tr>
    <td>468</td>
    <td>query_classifier : if parsing fails, parse tree and thread context are freed but used</td>
  </tr>
  <tr>
    <td>469</td>
    <td>rwsplit counts every connection twice in master - connection counts leak</td>
  </tr>
  <tr>
    <td>466</td>
    <td>hint_next_token doesn't detect <param>=<value> pair if there are no spaces around '='</td>
  </tr>
  <tr>
    <td>470</td>
    <td>Maxscale crashes after a normal query if a query with named hint was used before</td>
  </tr>
  <tr>
    <td>473</td>
    <td>Entering a hint with route server target as '=(' causes a crash</td>
  </tr>
  <tr>
    <td>472</td>
    <td>Using a named hint after its initial use causes a crash</td>
  </tr>
  <tr>
    <td>471</td>
    <td>Routing Hints route to server sometimes doesn't work</td>
  </tr>
  <tr>
    <td>463</td>
    <td>MaxScale hangs receiving more than 16K in input</td>
  </tr>
  <tr>
    <td>476</td>
    <td>mysql_common.c:protocol_archive_srv_command leaks memory and accesses freed memory</td>
  </tr>
  <tr>
    <td>479</td>
    <td>Undefined filter reference in maxscale.cnf causes a crash</td>
  </tr>
  <tr>
    <td>410</td>
    <td>maxscale.cnf server option is not parsed for spaces</td>
  </tr>
  <tr>
    <td>417</td>
    <td>Galera monitor freezes on network failure of a server</td>
  </tr>
  <tr>
    <td>488</td>
    <td>SHOW VARIABLES randomly failing with "Lost connection to MySQL server"</td>
  </tr>
  <tr>
    <td>484</td>
    <td>Hashtable does not always release write lock during add</td>
  </tr>
  <tr>
    <td>485</td>
    <td>Hashtable not locked soon enough in iterator get next item</td>
  </tr>
  <tr>
    <td>493</td>
    <td>Can have same section name multiple times without warning</td>
  </tr>
  <tr>
    <td>510</td>
    <td>Embedded library crashes on a call to free_embedded_thd</td>
  </tr>
  <tr>
    <td>511</td>
    <td>Format strings in log_manager.cc should be const char*</td>
  </tr>
  <tr>
    <td>509</td>
    <td>rw-split sensitive to order of terms in field list of SELECT</td>
  </tr>
  <tr>
    <td>507</td>
    <td>rw-split router does not send last_insert_id() to master</td>
  </tr>
  <tr>
    <td>490</td>
    <td>session handling for non-determinstic user variables broken</td>
  </tr>
  <tr>
    <td>489</td>
    <td>@@hostname and @@server_id treated differently from @@wsrep_node_address</td>
  </tr>
  <tr>
    <td>528</td>
    <td>Wrong service name in tee filter crashes maxscale on connect</td>
  </tr>
  <tr>
    <td>530</td>
    <td>MaxScale socket permission</td>
  </tr>
  <tr>
    <td>536</td>
    <td>log_manager doesn't write buffers to disk in the order they are written</td>
  </tr>
  <tr>
    <td>447</td>
    <td>Error log is flooded with same warning if there are no slaves present</td>
  </tr>
  <tr>
    <td>475</td>
    <td>The end comment tag in hints isn't properly detected.</td>
  </tr>
  <tr>
    <td>181</td>
    <td>Missing log entry if server not reachable</td>
  </tr>
  <tr>
    <td>486</td>
    <td>Hashtable problems when created with size less than one</td>
  </tr>
  <tr>
    <td>516</td>
    <td>maxadmin CLI client sessions are not closed?</td>
  </tr>
  <tr>
    <td>495</td>
    <td>Referring to a nonexisting server in servers=... doesn't even raise a warning</td>
  </tr>
  <tr>
    <td>538</td>
    <td>maxscale should expose details of "Down" server</td>
  </tr>
  <tr>
    <td>539</td>
    <td>MaxScale crashes in session_setup_filters</td>
  </tr>
  <tr>
    <td>494</td>
    <td>The service 'CLI' is missing a definition of the servers that provide the service</td>
  </tr>
  <tr>
    <td>180</td>
    <td>Documentation: No information found in the documentation about firewall settings</td>
  </tr>
  <tr>
    <td>524</td>
    <td>Connecting to MaxScale from localhost tries matching @127.0.0.1 grant</td>
  </tr>
  <tr>
    <td>481</td>
    <td>MySQL monitor doesn't set master server if the replication is broken</td>
  </tr>
  <tr>
    <td>437</td>
    <td>Failure to detect MHA master switch</td>
  </tr>
  <tr>
    <td>541</td>
    <td>Long queries cause MaxScale to block</td>
  </tr>
  <tr>
    <td>492</td>
    <td>In dcb.c switch fallthrough appears to be used without comment</td>
  </tr>
  <tr>
    <td>439</td>
    <td>Memory leak in getUsers</td>
  </tr>
  <tr>
    <td>545</td>
    <td>RWSplit: session modification commands weren't routed to all if executed inside open transaction</td>
  </tr>
  <tr>
    <td>543</td>
    <td>RWSplit router statistics counters are not updated correctly</td>
  </tr>
  <tr>
    <td>544</td>
    <td>server with weight=0 gets one connection</td>
  </tr>
  <tr>
    <td>525</td>
    <td>Crash when saving post in Wordpress</td>
  </tr>
  <tr>
    <td>533</td>
    <td>Drupal installer hangs</td>
  </tr>
  <tr>
    <td>497</td>
    <td>Can’t enable debug/trace logs in configuration file</td>
  </tr>
  <tr>
    <td>430</td>
    <td>Temporary tables not working in MaxScale</td>
  </tr>
  <tr>
    <td>527</td>
    <td>No signal handler for segfault etc</td>
  </tr>
  <tr>
    <td>546</td>
    <td>Use of weightby router parameter causes error log write</td>
  </tr>
  <tr>
    <td>506</td>
    <td>Don’t write shm/tmpfs by default without telling the user or giving a way to override it</td>
  </tr>
  <tr>
    <td>552</td>
    <td>Long argument options to maxadmin and maxscale broke maxadmin commands</td>
  </tr>
  <tr>
    <td>521</td>
    <td>Many commands in maxadmin client simply hang</td>
  </tr>
  <tr>
    <td>478</td>
    <td>Parallel session command processing fails</td>
  </tr>
  <tr>
    <td>499</td>
    <td>make clean leavessoem .o files behind</td>
  </tr>
  <tr>
    <td>500</td>
    <td>"depend: no such file warnings during make</td>
  </tr>
  <tr>
    <td>501</td>
    <td>log_manager, query classifier rebuilds unconditionally</td>
  </tr>
  <tr>
    <td>502</td>
    <td>log_manager and query_classifier builds always rebuild utils</td>
  </tr>
  <tr>
    <td>504</td>
    <td>clean rule for Documentation directory in wrong makefile</td>
  </tr>
  <tr>
    <td>505</td>
    <td>utils/makefile builds stuff unconditionally, misses "depend" target</td>
  </tr>
  <tr>
    <td>548</td>
    <td>MaxScale accesses freed client DCB and crashes</td>
  </tr>
  <tr>
    <td>550</td>
    <td>modutil functions process length incorrectly</td>
  </tr>
</table>


# Packaging

Both RPM and Debian packages are available for MaxScale in addition to the tar based releases previously distributed we now provide

* CentOS/RedHat 5 RPM

* CentOS/RedHat 6 RPM

* Ubuntu 14.04 package

