# MariaDB MaxScale 1.3 Release Notes

This document describes the changes in release 1.3, when compared to
release 1.2.1.

## 1.3.0 Beta

**NOTE** This is a beta release. We think it is better than 1.2.1,
but there may still be rough edges. Keep that in mind when trying it
out.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://mariadb.atlassian.net).

## New Features

### Persistent Connections

MaxScale 1.3.0 introduces the concept of *Persistent Connections*. With
that is meant that the connection from MaxScale to the backend server is
not terminated even if the connection from the client to MaxScale is.
If a client makes frequent short connections, there may be a benefit from
using the *Persistent Connection* feature as it may reduce the time it
takes from establishing a connection from the client through MaxScale to
the backend server.

Additional information is available in the following document:
* [Administration Tutorial](../Tutorials/Administration-Tutorial.md)

### Binlog Server

There are new administrative commands: STOP SLAVE, START SLAVE, RESET SLAVE
and CHANGE MASTER TO. The master server details are now provided by a
master.ini file located in binlog directory and could be changed via
CHANGE MASTER TO command issued via MySQL connection to MaxScale.

Before migrating to 1.3.0 it is necessary to put a writable master.ini file
into binlog directory, containing these parameters:

```
[binlog_configuration]
master_host=127.0.0.1
master_port=3308
master_user=repl
master_password=somepass
filestem=repl-bin
```

Users may change parameters according to their configuration.

**Note**: the "servers" parameter is no longer required in the service
definition.

Additional information is available in the following documents:
* [Binlogrouter Tutorial](../Tutorials/Replication-Proxy-Binlog-Router-Tutorial.md)
* [Upgrading binlogrouter to 1.3.0](../Upgrading/Upgrading-Binlogrouter-to-1.3.0.md)

### Logging Changes

Before 1.3, MaxScale logged data to four different log files; *error*,
*message*, *trace* and *debug*. Complementary and/or alternatively, MaxScale
could also log to syslog, in which case messages intended for the error and
message file were logged there. What files were enabled and written to was
controlled by entries in the MaxScale configuration file.

This has now been changed so that MaxScale logs to a single
file - *maxscale.log* - and each logged entry is prepended with *error*,
*warning*, *notice*, *info* or *debug*, depending on the seriousness or
priority of the message. The levels are the same as those of syslog.
MaxScale is still capable of complementary or alternatively logging to syslog.

What used to be logged to the *message* file is now logged as a *notice*
message and what used to be written to the *trace* file, is logged as an
*info* message.

By default, *notice*, *warning* and *error* messages are logged, while
*info* and *debug* messages are not. Exactly what kind of messages are
logged can be controlled via the MaxScale configuration file, but enabling
and disabling different kinds of messages can also be performed at runtime
from maxadmin.

Earlier, the *error* and *message* files were written to the filesystem,
while the *trace* and *debug* files were written to shared memory. The
one and only log file of MaxScale is now by default written to the filesystem.
This will have performance implications if *info* and *debug* messages are
enabled.

If you want to retain the possibility of turning on *info* and *debug*
messages, without it impacting the performance too much, the recommended
approach is to add the following entries to the MaxScale configuration file:

```
[maxscale]
syslog=1
maxlog=0
log_to_shm=1
```

This will have the effect of MaxScale creating the *maxscale.log* into
shared memory, but not logging anything to it. However, all *notice*,
*warning* and *error* messages will be logged to syslog.

Then, if there is a need to turn on *info* messages that can be done via
the maxadmin interface:

```
MaxScale> enable log-priority info
MaxScale> enable maxlog
```

Note that *info* and *debug* messages are never logged to syslog.

### PCRE2 integration

MaxScale now uses the PCRE2 library for regular expressions. This has been
integrated into the core configuration processing and most of the modules.
The main module which uses this is the regexfilter which now fully supports
the PCRE2 syntax with proper substitutions. For a closer look at how this
differs from the POSIX regular expression syntax take a look at the
[PCRE2 documentation](http://www.pcre.org/current/doc/html/pcre2syntax.html).

**Please note**, that the substitution string follows different rules than
the traditional substitution strings. The usual way of referring to capture
groups in the substitution string is with the backslash character followed
by the capture group reference e.g. `\1` but the PCRE2 library uses the dollar
character followed by the group reference. To quote the PCRE2 native API manual:

```
In the replacement string, which is interpreted as a UTF string in UTF mode, and is checked for UTF validity unless the PCRE2_NO_UTF_CHECK option is set, a dollar character is an escape character that can specify the insertion of characters from capturing groups in the pattern. The following forms are recognized:

  $$      insert a dollar character
  $<n>    insert the contents of group <n>
  ${<n>}  insert the contents of group <n>
```

### Improved launchable scripts

The launchable scripts were modified to allow usage without wrapper scripts.
The scripts are now executed as they are in the configuration files with certain
keywords being replaced with the initiator, event and node list. For more
details, please read the [Monitor Common](../Monitors/Monitor-Common.md) document.

## Bug fixes

Here is a list of bugs fixed since the release of MaxScale 1.2.1.

 * [MXS-414](https://mariadb.atlassian.net/browse/MXS-414): Maxscale crashed every day!
 * [MXS-415](https://mariadb.atlassian.net/browse/MXS-415): MaxScale 1.2.1 crashed with Signal 6 and 11
 * [MXS-351](https://mariadb.atlassian.net/browse/MXS-351): Router error handling can cause crash by leaving dangling DCB pointer
 * [MXS-428](https://mariadb.atlassian.net/browse/MXS-428): Maxscale crashes at startup.
 * [MXS-376](https://mariadb.atlassian.net/browse/MXS-376): MaxScale terminates with SIGABRT.
 * [MXS-269](https://mariadb.atlassian.net/browse/MXS-269): Crash in MySQL backend protocol
 * [MXS-500](https://mariadb.atlassian.net/browse/MXS-500): Tee filter hangs when statement aren't duplicated.
 * [MXS-447](https://mariadb.atlassian.net/browse/MXS-447): Monitors are started before they have been fully configured
 * [MXS-417](https://mariadb.atlassian.net/browse/MXS-417): Single character wildcard doesn't work in MaxScale
 * [MXS-409](https://mariadb.atlassian.net/browse/MXS-409): prepare should not hit all servers
 * [MXS-405](https://mariadb.atlassian.net/browse/MXS-405): Maxscale bin router crash
 * [MXS-412](https://mariadb.atlassian.net/browse/MXS-412): show dbusers segmentation fault
 * [MXS-289](https://mariadb.atlassian.net/browse/MXS-289): Corrupted memory or empty value are in Master_host field of SHOW SLAVE STATUS when master connection is broken
 * [MXS-283](https://mariadb.atlassian.net/browse/MXS-283): SSL connections leak memory
 * [MXS-54](https://mariadb.atlassian.net/browse/MXS-54): Write failed auth attempt to trace log
 * [MXS-501](https://mariadb.atlassian.net/browse/MXS-501): Use<db> hangs when Tee filter uses matching
 * [MXS-499](https://mariadb.atlassian.net/browse/MXS-499): Init script error on Debian Wheezy
 * [MXS-323](https://mariadb.atlassian.net/browse/MXS-323): mysql_client readwritesplit handleError seems using wrong dcb and cause wrong behavior
 * [MXS-494](https://mariadb.atlassian.net/browse/MXS-494): Weight calculation favors servers without connections
 * [MXS-493](https://mariadb.atlassian.net/browse/MXS-493): SIGFPE when weightby parameter is 0 and using LEAST_GLOBAL_CONNECTIONS
 * [MXS-492](https://mariadb.atlassian.net/browse/MXS-492): Segfault if server is missing weighting parameter
 * [MXS-360](https://mariadb.atlassian.net/browse/MXS-360): Persistent connections: maxadmin reports 0 all the time even if connections are created
 * [MXS-429](https://mariadb.atlassian.net/browse/MXS-429): Binlog Router crashes due to segmentation fault with no meaningful error if no listener is configured
 * [MXS-416](https://mariadb.atlassian.net/browse/MXS-416): Orphan sessions appear after many network errors
 * [MXS-472](https://mariadb.atlassian.net/browse/MXS-472): Monitors update status in multiple steps
 * [MXS-361](https://mariadb.atlassian.net/browse/MXS-361): crash on backend restart if persistent connections are in use
 * [MXS-403](https://mariadb.atlassian.net/browse/MXS-403): Monitor callback to DCBs evades thread control causing crashes
 * [MXS-392](https://mariadb.atlassian.net/browse/MXS-392): Update to "Rabbit MQ setup and MaxScale Integration" document
 * [MXS-491](https://mariadb.atlassian.net/browse/MXS-491): MaxScale can time out systemd if startup of services takes too long
 * [MXS-329](https://mariadb.atlassian.net/browse/MXS-329): The session pointer in a DCB can be null unexpectedly
 * [MXS-479](https://mariadb.atlassian.net/browse/MXS-479): localtime must not be used in the multi-threaded program.
 * [MXS-480](https://mariadb.atlassian.net/browse/MXS-480): Readwritesplit defaults cause connection pileup
 * [MXS-464](https://mariadb.atlassian.net/browse/MXS-464): Upgrade 1.2.0 to 1.2.1 blocking start of `maxscale` service
 * [MXS-365](https://mariadb.atlassian.net/browse/MXS-365): Load data local infile connection abort when loading certain files
 * [MXS-431](https://mariadb.atlassian.net/browse/MXS-431): Backend authentication fails with schemarouter
 * [MXS-394](https://mariadb.atlassian.net/browse/MXS-394): Faults in regex_replace function of regexfilter.c
 * [MXS-379](https://mariadb.atlassian.net/browse/MXS-379): Incorrect handing of a GWBUF may cause SIGABRT.
 * [MXS-321](https://mariadb.atlassian.net/browse/MXS-321): Incorrect number of connections in maxadmin list view
 * [MXS-413](https://mariadb.atlassian.net/browse/MXS-413): MaxAdmin hangs with show session
 * [MXS-408](https://mariadb.atlassian.net/browse/MXS-408): Connections to backend databases do not clear promptly
 * [MXS-385](https://mariadb.atlassian.net/browse/MXS-385): disable_sescmd_history can cause false data to be read.
 * [MXS-386](https://mariadb.atlassian.net/browse/MXS-386): max_sescmd_history should not close connections
 * [MXS-373](https://mariadb.atlassian.net/browse/MXS-373): If config file is non-existent, maxscale crashes.
 * [MXS-366](https://mariadb.atlassian.net/browse/MXS-366): Multi-source slave servers are not detected.
 * [MXS-271](https://mariadb.atlassian.net/browse/MXS-271): Schemarouter and unknown databases
 * [MXS-286](https://mariadb.atlassian.net/browse/MXS-286): Fix the content and format of MaxScale-HA-with-Corosync-Pacemaker document
 * [MXS-274](https://mariadb.atlassian.net/browse/MXS-274): Memory Leak
 * [MXS-254](https://mariadb.atlassian.net/browse/MXS-254): Failure to read configuration file results in no error log messages
 * [MXS-251](https://mariadb.atlassian.net/browse/MXS-251): Non-thread safe strerror
 * [MXS-291](https://mariadb.atlassian.net/browse/MXS-291): Random number generation has flaws
 * [MXS-342](https://mariadb.atlassian.net/browse/MXS-342): When ini_parse fails to parse config file, no log messages are printed.
 * [MXS-345](https://mariadb.atlassian.net/browse/MXS-345): maxscale.conf in /etc/init.d prevents puppet from starting maxscale
 * [MXS-333](https://mariadb.atlassian.net/browse/MXS-333): use_sql_variables_in=master doesn't work
 * [MXS-260](https://mariadb.atlassian.net/browse/MXS-260): Multiple MaxScale processes
 * [MXS-184](https://mariadb.atlassian.net/browse/MXS-184): init script issues in CentOS 7
 * [MXS-280](https://mariadb.atlassian.net/browse/MXS-280): SELECT INTO OUTFILE query succeeds even if backed fails
 * [MXS-202](https://mariadb.atlassian.net/browse/MXS-202): User password not handled correctly
 * [MXS-282](https://mariadb.atlassian.net/browse/MXS-282): Add example to "Routing Hints" document
 * [MXS-220](https://mariadb.atlassian.net/browse/MXS-220): LAST_INSERT_ID() query is redirect to slave if function call is in where clause
 * [MXS-196](https://mariadb.atlassian.net/browse/MXS-196): DCB state is changed prior to polling operation
 * [MXS-281](https://mariadb.atlassian.net/browse/MXS-281): SELECT INTO OUTFILE query goes several times to one slave
 * [MXS-197](https://mariadb.atlassian.net/browse/MXS-197): Incorrect sequence of operations with DCB
 * [MXS-195](https://mariadb.atlassian.net/browse/MXS-195): maxscaled.c ineffective DCB disposal
 * [MXS-363](https://mariadb.atlassian.net/browse/MXS-363): rpm building seems to do something wrong with maxscale libraries
 * [MXS-35](https://mariadb.atlassian.net/browse/MXS-35): bugzillaId-451: maxscale main() exit code is always 0 after it daemonizes
 * [MXS-29](https://mariadb.atlassian.net/browse/MXS-29): bugzillaId-589: detect if MAXSCALE_SCHEMA.HEARTBEAT table is not replicated
 * [MXS-436](https://mariadb.atlassian.net/browse/MXS-436): Invalid threads argument is ignored and MaxScale starts with one thread
 * [MXS-427](https://mariadb.atlassian.net/browse/MXS-427): Logging a large string causes a segmentation fault
 * [MXS-352](https://mariadb.atlassian.net/browse/MXS-352): With no backend connection, services aren't started
 * [MXS-293](https://mariadb.atlassian.net/browse/MXS-293): Bug in init script, and maxscale --user=maxscale does run as root
 * [MXS-210](https://mariadb.atlassian.net/browse/MXS-210): Check MaxScale user privileges
 * [MXS-111](https://mariadb.atlassian.net/browse/MXS-111): maxscale binlog events shown in show services seems to be double-counted for the master connection
 * [MXS-3](https://mariadb.atlassian.net/browse/MXS-3): Remove code for atomic_add in skygw_utils.cc
 * [MXS-258](https://mariadb.atlassian.net/browse/MXS-258): ERR_error_string could overflow in future
 * [MXS-310](https://mariadb.atlassian.net/browse/MXS-310): MaxScale 1.2 does not completely cleanly change to the maxscale user
 * [MXS-450](https://mariadb.atlassian.net/browse/MXS-450): Syslog default prefix is MaxScale not maxscale
 * [MXS-297](https://mariadb.atlassian.net/browse/MXS-297): postinstall on debian copies wrong file in /etc/init.d

## Known Issues and Limitations

There are a number bugs and known limitations within this version of MaxScale,
the most serious of this are listed below.

* MaxScale can not manage authentication that uses wildcard matching in hostnames in the mysql.user table of the backend database. The only wildcards that can be used are in IP address entries.

* When users have different passwords based on the host from which they connect MaxScale is unable to determine which password it should use to connect to the backend database. This results in failed connections and unusable usernames in MaxScale.

* LONGBLOB are currently not supported.

* Galera Cluster variables, such as @@wsrep_node_name, are not resolved by the embedded MariaDB parser.

* The Database Firewall filter does not support multi-statements. Using them will result in an error being sent to the client.

* The SSL support is known to be unstable.

## Packaging

RPM and Debian packages are provided for the Linux distributions supported
by MariaDB Enterprise.
