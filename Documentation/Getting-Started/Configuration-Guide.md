# MariaDB MaxScale Configuration Guide

[TOC]

# Introduction

This document describes how to configure MariaDB MaxScale and presents some
possible usage scenarios. MariaDB MaxScale is designed with flexibility in mind,
and consists of an event processing core with various support functions and
plugin modules that tailor the behavior of the program.

# Concepts

## Glossary

Word | Description
--------------------|----------------------------------------------------
connection routing  | Connection routing is a method of handling requests in which MariaDB MaxScale will accept connections from a client and route data on that connection to a single database using a single connection. Connection based routing will not examine individual requests on a connection and it will not move that connection once it is established.
statement routing   | Statement routing is a method of handling requests in which each request within a connection will be handled individually. Requests may be sent to one or more servers and connections may be dynamically added or removed from the session.
module              | A module is a separate code entity that may be loaded dynamically into MariaDB MaxScale to increase the available functionality. Modules are implemented as run-time loadable shared objects.
connection failover | When a connection currently being used between MariaDB MaxScale and the database server fails a replacement will be automatically created to another server by MariaDB MaxScale without client intervention
backend database    | A term used to refer to a database that sits behind MariaDB MaxScale and is accessed by applications via MariaDB MaxScale.
REST API | HTTP administrative interface

## Objects

### Server

A server represents an individual database server to which a client can be
connected via MariaDB MaxScale. The status of a server varies during the lifetime
of the server and typically the status is updated by some monitor. However, it
is also possible to update the status of a server manually.

Status | Description
-------|------------
Running       | The server is running.
Master        | The server is the primary.
Slave         | The server is a replica.
Draining      | The server is being drained. Existing connections can continue to be used, but no new connections will be created to the server. Typically this status bit is turned on manually using _maxctrl_, but a monitor may also turn it on.
Drained       | The server has been drained. The server was being drained and now the number of connections to the server has dropped to 0.
Auth Error    | The monitor cannot login and query the server due to insufficient privileges.
Maintenance   | The server is under maintenance. Typically this status bit is turned on manually using _maxctrl_, but it will also be turned on for a server that for some reason is blocking connections from MaxScale. When a server is in maintenance mode, no connections will be created to it and existing connections will be closed.
Slave of External Master | The server is a replica of a primary that is not being monitored.

For more information on how to manually set these states via MaxCtrl, read the
[Administration Tutorial](../Tutorials/Administration-Tutorial.md).

### Monitor

A monitor module is capable of monitoring the state of a particular kind
of cluster and making that state available to the routers of MaxScale.

Examples of monitor modules are `mariadbmon` that is capable of monitoring
a regular primary-replica cluster and in addition of performing both _switchover_
and _failover_, `galeramon` that is capable of monitoring a Galera cluster,
and `csmon` that is capable of monitoring a Columnstore cluster.

Monitor modules have sections of their own in the MaxScale configuration
file.

### Filter

A filter module resides in front of routers in the request processing chain
of MaxScale. That is, a filter will see a request before it reaches the router
and before a response is sent back to the client. This allows filters to
reject, handle, alter or log information about a request.

Examples of filters `cache` that provides query caching according to rules,
`regexfilter` that can rewrite requests according to regular expressions, and
`qlafilter` that logs information about requests.

Filters have sections of their own in the MaxScale configuration file that are
referred to from _services_.

### Router

A router module is capable of routing requests to backend servers according to
the characteristics of a request and/or the algorithm the router
implements. Examples of routers are `readconnroute` that provides _connection
routing_, that is, the server is chosen according to specified rules when the
session is created and all requests are subsequently routed to that server,
and `readwritesplit` that provides _statement routing_, that is, each
individual request is routed to the most appropriate server.

Routers do not have sections of their own in the MaxScale configuration file,
but are referred to from _services_.

### Service

A service abstracts a set of databases and makes them appear as a single one
to the client. Depending on what router (e.g. `readconnroute` or
`readwritesplit`) the service uses, the servers are used in some particular
way. If the service uses filters, then all requests will be pre-processed in
some way before they reach the router.

Services have sections of their own in the MaxScale configuration file.

### Listener

A listener defines a port MaxScale listens on. Connection requests arriving on
that port will be forwarded to the service the listener is associated with. A
listener may be associated with a single service, but several listeners may be
associated with the same service.

Listeners have sections of their own in the MaxScale configuration file.

### Include

An [include section](#include-1) defines common parameters used in other
configuration sections.

# Administration

The administration of MaxScale can be divided in two parts:

* Writing the MaxScale configuration file, which is described in the following
  [section](#configuration).
* Performing runtime modifications using [MaxCtrl](../Reference/MaxCtrl.md)

For detailed information about _MaxCtrl_ please refer to the specific
documentation referred to above. In the following it will only be explained how
MaxCtrl relate to each other, as far as user credentials go.

**Note**: By default all runtime configuration changes are saved on disk and
  loaded on startup. Refer to the
  [Dynamic Configuration](#dynamic-configuration) section for more details
  on how it works and how to disable it.

MaxCtrl can connect using TCP/IP sockets. When connecting with MaxCtrl using
TCP/IP sockets, the user and password must be provided and are checked against a
separate user credentials database. By default, that database contains the user
`admin` whose password is `mariadb`.

Note that if MaxCtrl is invoked without explicitly providing a user and password
then it will by default use `admin` and `mariadb`. That means that when the
default user is removed, the credentials must always be provided.

## Administration audit file

The REST API calls to MaxScale can be logged
by enabling [admin_audit](#admin_audit).

For more detail see the admin audit configuration values `admin_audit`,
`admin_audit_file` and `admin_audit_exclude_methods` below
and [Administration Tutorial](../Tutorials/Administration-Tutorial.md#administration-audit-file).

## Static Configuration Parameters

The following list of global configuration parameters can **NOT** be changed at
runtime and can only be defined in a configuration file:

* `admin_auth`
* `admin_enabled`
* `admin_gui`
* `admin_host`
* `admin_pam_readonly_service`
* `admin_pam_readwrite_service`
* `admin_readonly_hosts`
* `admin_readwrite_hosts`
* `admin_port`
* `admin_secure_gui`
* `admin_ssl_ca`
* `admin_ssl_version`
* `admin_jwt_algorithm`
* `admin_jwt_key`
* `admin_jwt_issuer`
* `auto_tune`
* `cachedir`
* `connector_plugindir`
* `datadir`
* `debug`
* `execdir`
* `language`
* `libdir`
* `load_persisted_configs`
* `persist_runtime_changes`
* `local_address`
* `log_augmentation`
* `log_warn_super_user`
* `logdir`
* `module_configdir`
* `persistdir`
* `piddir`
* `query_retries`
* `sharedir`
* `sql_mode`
* `substitute_variables`
* `threads_max`

All other parameters that relate to objects can be altered at runtime or can be
changed by destroying and recreating the object in question.

# Configuration

MaxScale by default reads configuration from the file `/etc/maxscale.cnf`. If
the command line argument `--configdir=<path>` is given, `maxscale.cnf` is
searched for in *\<path\>* instead.  If the argument `--config=<file>` is given,
configuration is read from the file *\<file\>*.

MaxScale also looks for a directory with the same name as the configuration
file, followed by ".d" (for example `/etc/maxscale.cnf.d`). If found, MaxScale
recursively reads all files with the ".cnf" suffix in the directory hierarchy.
Other files are ignored.

After loading normal configuration files, MaxScale reads runtime-generated
configuration files, if any, from the
[persisted configuration files directory](#persistdir).

Different configuration sections can be arranged with little restrictions.
Global path settings such as `logdir`, `piddir` and `datadir` are only read from
the main configuration file.  Other global settings are also best left in the
main file to ensure they are read before other configuration sections are
parsed.

The configuration file format used is
[INI](https://en.wikipedia.org/wiki/INI_file), similar to the
MariaDB Server. The files contain sections and each section can contain multiple
key-value pairs.

Comments are defined by prefixing a row with a hash (#). Trailing comments are
not supported.

```
# This is a comment before a parameter
some_parameter=123
```

A parameter can be defined on multiple lines as shown below. A value spread over
multiple lines is simply concatenated. The additional lines of the value
definition need to have at least one whitespace character in the beginning.
```
[MyService]
type=service
router=readconnroute
servers=server1,
        server2,
        server3
```

## Names

Section names may not contain whitespace and must not start with the characters
`@@`.

As the object names are used to form URLs in the MaxScale REST API, they must be
safe for use in URLs. This means that only alphanumeric characters (i.e. `a-z`
`A-Z` and `0-9`) and the special characters `_.~-` can be used.

## Dynamic Configuration

By default all changes done at runtime via the MaxScale GUI, MaxCtrl or the REST
API will be saved on disk, inside the [persistdir](#persistdir) directory. The
changes done at runtime will override the configuration found in the static
configuration files for that particular object.

This means that if an object that is found in `/etc/maxscale.cnf` is modified at
runtime, all future changes to it must also be done at runtime. Any
modifications done to `/etc/maxscale.cnf` after a runtime change has been made
are ignored for that object.

To prevent the saving of runtime changes and to make all runtime changes
volatile, add [`persist_runtime_changes=false`](#persist_runtime_changes) and
[`load_persisted_configs=false`](#load_persisted_configs) under the `[maxscale]`
section. This will make MaxScale behave like the MariaDB server does: any
changes done with `SET GLOBAL` statements are lost if the process is restarted.

## Special Parameter Types

### Booleans

Boolean type parameters interpret the values `true`, `yes`, `on` and `1` as
_true_ values and `false`, `no`, `off` and `0` as _false_ values. Starting with
MaxScale 23.02, the REST API also accepts the same boolean values for boolean
type parameters.

### Sizes

Where _specifically noted_, a number denoting a size can be suffixed by a subset
of the IEC binary prefixes or the SI prefixes. In the former case the number
will be interpreted as a certain multiple of 1024 and in the latter case as a
certain multiple of 1000. The supported IEC binary suffixes are `Ki`, `Mi`, `Gi`
and `Ti` and the supported SI suffixes are `k`, `M`, `G` and `T`. In both cases,
the matching is case-insensitive.

For instance, the following entries
```
max_size=1099511628000
max_size=1073741824Ki
max_size=1048576Mi
max_size=1024Gi
max_size=1Ti
```
are equivalent, as are the following
```
max_size=1000000000000
max_size=1000000000k
max_size=1000000M
max_size=1000G
max_size=1T
```

### Durations

A number denoting a duration can be suffixed by one of the case-insensitive
suffixes `h`, `m` or `min`, `s` and `ms`, for specifying durations in hours,
minutes, seconds and milliseconds, respectively.

For instance, the following entries
```
soft_ttl=1h
soft_ttl=60m
soft_ttl=60min
soft_ttl=3600s
soft_ttl=3600000ms
```
are equivalent.

Note that if an explicit unit is not specified, then it is specific to the
configuration parameter whether the duration is interpreted as seconds or
milliseconds.

_Not_ providing an explicit unit has been deprecated in MaxScale 2.4.

### Percent

A number denoting a percent must be suffixed with `%`.

For instance
```
some_param=42%
```

### Regular Expressions

Many modules have settings which accept a regular expression. In most cases, these
settings are named either *match* or *exclude*, and are used to filter users or queries.
MaxScale uses the [PCRE2-library](https://www.pcre.org/current/doc/html/) for matching
regular expressions.

When writing a regular expression (regex) type parameter to a MaxScale configuration file,
the pattern string should be enclosed in slashes e.g. `^select` -> `match=/^select/`. This
clarifies where the pattern begins and ends, even if it includes whitespace. Without
slashes the configuration loader trims the pattern from the ends. The slashes are removed
before compiling the pattern. For backwards compatibility, the slashes are not yet
mandatory. Omitting them is, however, deprecated and will be rejected in a future release
of MaxScale. Currently, *binlogfilter*, *ccrfilter*, *qlafilter*, *tee* and *avrorouter*
accept parameters in this type of regular expression form. Some other modules may not
handle the slashes yet correctly.

PCRE2 supports a complicated regular expression
[syntax](https://www.pcre.org/current/doc/html/pcre2syntax.html). MaxScale typically uses
regular expressions simply, only checking whether the pattern and subject match at some
point. For example, using the QLAFilter and setting `match=/SELECT/` causes the filter to
accept any query with the text "SELECT" somewhere within. To force the pattern to only
match at the beginning of the query, set `match=/^SELECT/`. To only match the end, set
`match=/SELECT$/`.

Modules which accept regular expression parameters also often accept options which affect
how the patterns are compiled. Typically, this setting is called *options* and accepts
values such as `ignorecase`, `case` and `extended`.

* `ignorecase`: Causes the regular expression matcher to ignore letter case, and
  is often on by default. When enabled, `/SELECT/` would match both `SELECT` and
  `select`.

* `extended`: Ignores whitespace and `#` comments in the pattern. Note that this
  is not the same as the extended regular expression syntax that for example
  `grep -E` uses.

* `case`: Turns on case-sensitive matching. This means that `/SELECT/` will not
  match `select`.

These settings can also be defined in the pattern itself, so they can be
used even in modules without pattern compilation settings. The pattern
settings are `(?i)` for `ignorecase` and `(?x)` for `extended`. See the
[PCRE2 syntax documentation](https://www.pcre.org/current/doc/html/pcre2syntax.html#SEC16)
for more information.

#### Standard regular expression settings for filters

Many filters use the settings *match*, *exclude* and *options*. Since these settings are
used in a similar way across these filters, the settings are explained here. The
documentation of the filters link here and describe any exceptions to this
generalized explanation.

These settings typically limit the queries the filter module acts on. *match* and
*exclude* define PCRE2 regular expression patterns while *options* affects how both of the
patterns are compiled. *options* works as explained above, accepting the values
`ignorecase`, `case` and `extended`, with `ignorecase` being the default.

The queries are matched as they arrive to the filter on their way to a routing module. If
*match* is defined, the filter only acts on queries matching that pattern. If *match* is
not defined, all queries are considered to match.

If *exclude* is defined, the filter only acts on queries not matching that pattern. If
*exclude* is not defined, nothing is excluded.

If both are defined, the query needs to match *match* but not match *exclude*.

Even if a filter does not act on a query, the query is not lost. The query is simply
passed on to the next module in the processing chain as if the filter was not there.

### Enumerations

Enumeration type parameters have a pre-defined set of accepted values. For types
declared as `enum`, only one value is accepted. For `enum_mask` types, multiple
values can be defined by separating them with commas. All enumeration values in
MaxScale are case-sensitive.

For example the `router_options` parameter in the `readconnroute` router is a
mask type enumeration:

```
router_options=master,slave
```

### Path Lists

A `pathlist` type parameter expects one or more filesystem paths separated by
colons. The value must not include space between the separators.

Here is an example path list parameter that points to `/tmp/something.log` and
`/var/log/maxscale/maxscale.log`:

```
path_list_parameter=/tmp/something.log:/var/log/maxscale/maxscale.log
```

## Global Settings

The global settings, in a section named `[MaxScale]`, allow various parameters
that affect MariaDB MaxScale as a whole to be tuned. This section must be
defined in the root configuration file which by default is `/etc/maxscale.cnf`.

### `auto_tune`

- **Type**: string list
- **Values**: `all` or list of auto tunable parameters, separated by `,`
- **Default**: No
- **Mandatory**: No
- **Dynamic**: No

An _auto tunable_ parameter is a parameter whose value can be derived from a
particular server variable. With this parameter it can be specified whether
`all` or a specific set of parameters should automatically be set.

The current auto tunable parameters are:

|MaxScale Parameter|Server Variable Dependency|
|------------------|--------------------------|
|[connection_keepalive](#connection_keepalive)|80% of the smallest [`wait_timeout`](https://mariadb.com/docs/reference/mdb/system-variables/wait_timeout/) value of the servers used by the service|
|[wait_timeout](#wait_timeout)|The smallest [`wait_timeout`](https://mariadb.com/docs/reference/mdb/system-variables/wait_timeout/) value of the servers used by the service|

The values of the server variables are collected by monitors, which means that
if the servers of a service are not monitored by a monitor, then the parameters
of that service will not be auto tuned.

Note that even if `auto_tune` is set to `all`, the auto tunable parameters
can still be set in the configuration file and modified with _maxctrl_.
However, the specified value will be overwritten at the next auto tuning
round, but only if the servers of the service are monitored by a monitor.

### `threads`

- **Type**: positive integer or `auto`
- **Default**: `auto`
- **Dynamic**: Yes

This parameter controls the number of worker threads that are routing
client traffic. The default is `auto` which uses as many threads
as there are CPU cores. MaxScale versions older than 6 used one thread by
default.

You can explicitly enable automatic configuration of this value by setting the
value to `auto`. This way MariaDB MaxScale will detect the number of available
processors and set the amount of threads to be equal to that number.

Note that if MaxScale is running in a container where the CPU resources
have been limited, the use of `auto` may cause MaxScale to use more resources
than what is available. In such a situation `auto` should not be used, but instead
an explicit number that corresponds to the amount of CPU resources available in
the container. As a rule of thumb, an appropriate value for `threads` is the
_vCPU_ of the container rounded up to the nearest integer. For instance, if
the _vCPU_ of the container is `0.5` then `1` is an appropriate value for
`threads`, if the _vCPU_ is `2.3` then `3` is.

The maximum value for `threads` is specified by [threads_max](#threads_max).

```
# Valid options are:
#       threads=[<number of threads> | auto ]

[MaxScale]
threads=auto
```

From 23.02 onwards it is possible to change the number threads at runtime.
Please see [Threads](#threads-1) for more details.

Additional threads will be created to execute other internal services within
MariaDB MaxScale. This setting is used to configure the number of threads that
will be used to manage the user connections.

### `threads_max`

- **Type**: positive integer
- **Default**: 256
- **Dynamic**: No

This parameter specifies the hard limit for the number of worker threads,
which is specified using [threads](#threads).

At startup, if the value of `threads` is larger than that of `threads_max`,
the value of `threads` will be reduced to that. At runtime, an attempt to
increase the value of `threads` beyond that of `threads_max` is an error.

### `rebalance_period`

This duration parameter controls how often the load of the worker threads
should be checked. The default value is 0, which means that no checks and
no rebalancing will be performed.
```
rebalance_period=10s
```
Note that the value of `rebalance_period` should not be smaller than the
value of `rebalance_window` whose default value is 10.

If the value of `rebalance_period` is significantly shorter than that
of `rebalance_window`, it may lead to oscillation where work is constantly
moved from one thread to another.

### `rebalance_threshold`

This integer parameter controls at which point MaxScale should start
moving work from one worker thread to another.

If the difference in load between the thread with the maximum load and
the thread with the minimum load is larger than the value of this parameter,
then work will be moved from the former to the latter.

Although the load of a thread can vary between 0 and 100, the value of this
parameter must be between 5 and 100. The default value is 20.
```
rebalance_threshold=15
```
Note that rebalancing will not be performed unless `rebalance_period`
has been specified.

### `rebalance_window`

This integer parameter controls how many seconds of load should be
taken into account when deciding whether work should be moved from
one thread to another.

The default value is 10, which means that the load during the last 10
seconds is considered when deciding whether work should be moved.

The minimum value is 1 and the maximum 60.

### `skip_name_resolve`

- **Type**: boolean
- **Default**: false
- **Dynamic**: Yes

This parameter controls whether reverse domain name lookups are made to convert
client IP addresses to hostnames. If enabled, client IP addresses will not be
resolved to hostnames during authentication or for the REST API even if requested.

If you have database users that use a hostname in the host part of the user
(i.e. `'user'@'my-hostname.org'`), a reverse lookup on the client IP address is
done to see if it matches the host. Reverse DNS lookups can be very slow which
is why it is recommended that they are disabled and that users are defined using
an IP address.

### `auth_connect_timeout`

Duration, default 10s. This setting defines the connection timeout when
attempting to fetch MariaDB/MySQL/Clustrix users from a backend server. The same
value is also used for read and write timeouts. Increasing this value causes
MaxScale to wait longer for a response from a server before user fetching fails.
Other servers may then be attempted.

```
auth_connect_timeout=10s
```

The value is given as [a duration](#durations). If no explicit unit is provided,
the value is interpreted as seconds. In subsequent versions a value without a
unit may be rejected. Since the granularity of the timeout is seconds, a timeout
specified in milliseconds will be rejected even if the given value is longer
than a second.

### `auth_read_timeout`

Deprecated and ignored as of MaxScale 2.5.0. See *auth_connect_timeout* above.

### `auth_write_timeout`

Deprecated and ignored as of MaxScale 2.5.0. See *auth_connect_timeout* above.

### `query_retries`

The number of times an interrupted internal query will be retried. The default
is to retry the query once. This feature was added in MaxScale 2.1.10 and was
disabled by default until MaxScale 2.3.0.

An interrupted query is any query that is interrupted by a network
error. Connection timeouts are included in network errors and thus is it
advisable to make sure that the value of `query_retry_timeout` is set to an
adequate value. Internal queries are only used to retrieve authentication data
and monitor the servers.

### `query_retry_timeout`

The total timeout in seconds for any retried queries. The default value is 5
seconds.

An interrupted query is retried for either the configured amount of attempts or
until the configured timeout is reached.

The value is specified as documented [here](#durations). If no explicit unit
is provided, the value is interpreted as seconds in MaxScale 2.4. In subsequent
versions a value without a unit may be rejected. Note that since the granularity
of the timeout is seconds, a timeout specified in milliseconds will be rejected,
even if the duration is longer than a second.

### `passive`

- **Type**: [boolean](#booleans)
- **Default**: false
- **Dynamic**: Yes

Controls whether MaxScale is a passive node in a cluster of multiple MaxScale
instances.

This parameter is intended to be used with multiple MaxScale instances that use
failover functionality to manipulate the cluster in some form. Passive nodes
only observe the clusters being monitored and take no direct actions.

The following functionality is disabled when passive mode is enabled:

 * Automatic failover in the `mariadbmon` module
 * Automatic rejoin in the `mariadbmon` module
 * Launching of monitor scripts

**NOTE:** Even if MaxScale is in passive mode, it will still accept clients and
  route any traffic sent to it. The **only** operations affected by the passive
  mode are the ones listed above.

### `ms_timestamp`

- **Type**: [boolean](#booleans)
- **Default**: false
- **Dynamic**: Yes

Enable or disable the high precision timestamps in logfiles. Enabling this adds
millisecond precision to all logfile timestamps.

### `syslog`

- **Type**: [boolean](#booleans)
- **Default**: false
- **Dynamic**: Yes

Log messages to the system journal. This logs messages using the native SystemD
journal interface. The logs can be viewed with `journalctl`.

MaxScale 22.08 changed the default value of `syslog` from `true` to
`false`. This was done to remove the redundant logging that it caused as both
`syslog` and `maxlog` were enabled by default. This caused each message to be
logged twice: once into the system journal and once into MaxScale's own logfile.

### `maxlog`

- **Type**: [boolean](#booleans)
- **Default**: true
- **Dynamic**: Yes

Log messages to MariaDB MaxScale's log file. The name of the log file is
`maxscale.log` and it is located in the directory pointed by [logdir](#logdir).

### `log_warning`

- **Type**: [boolean](#booleans)
- **Default**: true
- **Dynamic**: Yes

Log messages whose syslog priority is *warning*.

MaxScale logs warning level messages whenever a condition is encountered that
the user should be notified of but does not require immediate action or it
indicates a minor problem.

### `log_notice`

- **Type**: [boolean](#booleans)
- **Default**: true
- **Dynamic**: Yes

Log messages whose syslog priority is *notice*.

These messages contain information that is helpful for the user and they usually
do not indicate a problem. These are logged whenever something worth nothing
happens in either MaxScale or in the servers it monitors.

### `log_info`

- **Type**: [boolean](#booleans)
- **Default**: false
- **Dynamic**: Yes

Log messages whose syslog priority is *info*.

These messages provide detailed information about the internal workings of
MariaDB MaxScale. These messages should only be enabled when there is a need to
inspect the internal logic of MaxScale. A common use-case is to see why a
particular query was handled in a certain way. Almost all modules log some
messages on the info level and this can be very helpful when trying to solve
routing related problems.

### `log_debug`

- **Type**: [boolean](#booleans)
- **Default**: false
- **Dynamic**: Yes

Log messages whose syslog priority is *debug*.

These messages are intended for development purposes and are disabled by
default. These are rarely useful outside of debugging core MaxScale issues.

**Note:** If MariaDB MaxScale has been built in release mode, then debug
messages are excluded from the build and this setting will not have any
effect. If an attempt to enable these is made, a warning is logged.

### `log_warn_super_user`

- **Type**: [boolean](#booleans)
- **Default**: false
- **Dynamic**: No

When enabled, a warning is logged whenever a client with SUPER-privilege
successfully authenticates. This also applies to COM_CHANGE_USER-commands. The
setting is intended for diagnosing situations where a client interferes with a
primary server switchover. Super-users bypass the *read_only*-flag which
switchover uses to block writes to the primary.

### `log_augmentation`

- **Type**: integer
- **Default**: 0
- **Dynamic**: Yes

Enable or disable the augmentation of messages. If this is enabled, then each
logged message is appended with the name of the function where the message was
logged. This is primarily for development purposes and hence is disabled by
default.

```
# Valid options are:
#       log_augmentation=<0|1>
log_augmentation=1
```

To disable the augmentation use the value 0 and to enable it use the value 1.

### `log_throttling`

It is possible that a particular error (or warning) is logged over and over
again, if the cause for the error persistently remains. To prevent the log from
flooding, it is possible to specify how many times a particular error may be
logged within a time period, before the logging of that error is suppressed for
a while.

```
# A valid value looks like
#       log_throttling = X, Y, Z
#
# where the first value X is a positive integer and means the number of times
# a specific error may be logged within a duration of Y, before the logging
# of that error is suppressed for a duration of Z.
log_throttling=8, 2s, 15000ms
```

In the example above, the logging of a particular error will be suppressed for
15 seconds if the error has been logged 8 times in 2 seconds.

The default is `10, 1000ms, 10000ms`, which means that if the same error is
logged 10 times in one second, the logging of that error is suppressed for the
following 10 seconds.

Whenever an error message that is being throttled is logged within the
triggering window (the second argument), the suppression window is
extended. This continues until there is a pause in the messages that is longer
than the triggering window.

For example, with the default configuration the messages must pause for at least
one second in order for the throttling to eventually stop. This mechanism
prevents long-lasting error conditions from slowly filling up the log with short
bursts of messages.

To disable log throttling, add an entry with an empty value

```
log_throttling=
```
or one where any of the integers is 0.

```
log_throttling=0, 0, 0
```
The durations can be specified as documented [here](#durations). If no explicit
unit is provided, the value is interpreted as milliseconds in MaxScale 2.4. In
subsequent versions a value without a unit may be rejected.

Note that *notice*, *info* and *debug* messages are never throttled.

### `logdir`

Set the directory where the logfiles are stored. The folder needs to be both
readable and writable by the user running MariaDB MaxScale.

The default value is `/var/log/maxscale/`.

```
logdir=/var/log/maxscale/
```

### `datadir`

Set the directory where the data files used by MariaDB MaxScale are stored.
Modules can write to this directory and for example the binlogrouter uses this
folder as the default location for storing binary logs.

This is also the directory where the password encryption key is read from that
is generated by `maxkeys`.

The default value is `/var/lib/maxscale/`.

```
datadir=/var/lib/maxscale/
```

### `libdir`

Set the directory where MariaDB MaxScale looks for modules. The library
directory is the only directory that MariaDB MaxScale uses when it searches for
modules. If you have custom modules for MariaDB MaxScale, make sure you have
them in this folder.

The default value depends on the operating system. For RHEL versions the value
is `/usr/lib64/maxscale/`. For Debian and Ubuntu it is
`/usr/lib/x86_64-linux-gnu/maxscale/`

```
libdir=/usr/lib64/maxscale/
```

### `sharedir`

Sets the directory where static data assets are loaded.

The MaxScale GUI static files are located in the `gui/` subdirectory. If the GUI
files have been manually moved somewhere else, this path must be configured to
point to the parent directory of the `gui/` subdirectory.

The MaxScale REST API only serves files for the GUI that are located in the
`gui/` subdirectory of the configured `sharedir`. Any files whose real path
resolves to outside of this directory are not served by the MaxScale GUI: this
is done to prevent other files from being accessible via the MaxScale REST
API. This means that path to the GUI source directory can contain symbolic links
but all parts after the `/gui/` directory must reside inside it.

The default value is `/usr/share/maxscale/`.

### `cachedir`

Configure the directory MariaDB MaxScale uses to store cached data.

The default value is `/var/cache/maxscale/`.

```
cachedir=/var/cache/maxscale/
```

### `piddir`

Configure the directory for the PID file for MariaDB MaxScale. This file
contains the Process ID for the running MariaDB MaxScale process.

The default value is `/var/run/maxscale/`.

```
piddir=/var/run/maxscale/
```

### `execdir`

Configure the directory where the executable files reside. All internal
processes which are launched will use this directory to look for executable
files.

The default value is `/usr/bin/`.

```
execdir=/usr/bin/
```

### `connector_plugindir`

Location of the MariaDB Connector-C plugin directory. The MariaDB Connector-C
used in MaxScale can use this directory to load authentication plugins. The
versions of the plugins must be binary compatible with the connector version
that MaxScale was built with.

Starting with version 6.2.0, the plugins are bundled with MaxScale and the
default value now points to the bundled plugins. The location where the plugins
are stored depends on the operating system. For RHEL versions the value is
`/usr/lib64/maxscale/plugin/`. For Debian and Ubuntu it is
`/usr/lib/x86_64-linux-gnu/maxscale/plugin/`.

Older versions of MaxScale used `/usr/lib/mysql/plugin/` as the default value.

```
connector_plugindir=/usr/lib64/maxscale/plugin/
```

### `persistdir`

Configure the directory where persisted configurations are stored. When a new
object is created via MaxCtrl, it will be stored in this directory. Do not use
this directory for normal configuration files, use _/etc/maxscale.cnf.d/_
instead. The user MaxScale is running as must be able to write into this
directory.

The default value is `/var/lib/maxscale/maxscale.cnf.d/`.

```
persistdir=/var/lib/maxscale/maxscale.cnf.d/
```

### `module_configdir`

Configure the directory where module configurations are stored. Path arguments
are resolved relative to this directory. This directory should be used to store
module specific configurations.

Any configuration parameter that is not an absolute path will be interpreted as
a relative path. The relative paths use the module configuration directory as
the working directory.

For example, the configuration parameter `file=my_file.txt` would be interpreted
as `/etc/maxscale.modules.d/my_file.txt` whereas `file=/home/user/my_file.txt` would
be interpreted as `/home/user/my_file.txt`.

The default value is `/etc/maxscale.modules.d/`.

```
module_configdir=/etc/maxscale.modules.d/
```

### `language`

Set the folder where the errmsg.sys file is located in. MariaDB MaxScale will
look for the errmsg.sys file installed with MariaDB MaxScale from this folder.

The default value is `/var/lib/maxscale/`.

```
language=/var/lib/maxscale/
```

### `query_classifier`

Deprecated since MariaDB MaxScale 23.08.

### `query_classifier_cache_size`

Specifies the maximum size of the query classifier cache. The default limit is
15% of total system memory starting with MaxScale 2.3.7. In older versions the
default limit was 40% of total system memory. This feature was added in MaxScale
2.3.0.

When the query classifier cache has been enabled, MaxScale will, after a
statement has been parsed, store the classification result using the
canonicalized version of the statement as the key.

If the classification result for a statement is needed, MaxScale will first
canonicalize the statement and check whether the result can be found in the
cache.  If it can, the statement will not be parsed at all but the cached result
is used.

The configuration parameter takes one integer that specifies the maximum size of
the cache. The size of the cache can be specifed as explained [here](#sizes).

```
# 1MB query classifier cache
query_classifier_cache_size=1MB
```

Note that MaxScale uses a separate cache for each worker thread. To obtain the
amount of memory available for each thread, divide the cache size with the value
of `threads`. If statements are evicted from the cache (visible in the
diagnostic output), consider increasing the cache size.

Note also that limit is not a hard limit, but an approximate one. Namely, although
the memory needed for storing the canonicalized statement and the classification
result is correctly accounted for, there is additional overhead whose size is not
exactly known and over which we do not have direct control.

Using `maxctrl show threads` it is possible to check what the actual size of
the cache is and to see performance statistics.

Key|Meaning
---|-------
QC cache size|The current size of the cache (bytes).
QC cache inserts|How many entries have been inserted into the cache.
QC cache hits|How many times the classification result has been found from the cache.
QC cache misses|How many times the classification result has not been found from the cache, but the classification had to be performed.
QC cache evictions|How many times a cache entry has had to be removed from the cache, in order to make place for another.

### `query_classifier_args`

Deprecated since MariaDB MaxScale 23.08.

### `substitute_variables`

- **Type**: [boolean](#booleans)
- **Default**: false
- **Dynamic**: No

Enable or disable the substitution of environment variables in the MaxScale
configuration file. If the substitution of variables is enabled and a
configuration line like
```
some_parameter=$SOME_VALUE
```
is encountered, then `$SOME_VALUE` will be replaced with the actual value
of the environment variable `SOME_VALUE`. Note:
* Variable substitution will be made _only_ if '$' is the first character
  of the value.
* _Everything_ following '$' is interpreted as the name of the environment
  variable.
* Referring to a non-existing environment variable is a fatal error.

```
substitute_variables=true
```

The setting of `substitute_variables` will have an effect on all parameters
in the all other sections, irrespective of where the `[maxscale]` section
is placed in the configuration file. However, in the `[maxscale]` section,
to ensure that substitution will take place, place the
`substitute_variables=true` line first.

### `sql_mode`

Specifies whether the query classifier parser should initially expect _MariaDB_
or _PL/SQL_ kind of SQL.

The allowed values are:
   `default`: The parser expects regular _MariaDB_ SQL.
   `oracle` : The parser expects PL/SQL.

```
sql_mode=oracle
```

The default value is `default`.

**NOTE** If `sql_mode` is set to `oracle`, then MaxScale will also assume
that `autocommit` initially is off.

At runtime, MariaDB MaxScale will recognize statements like
```
set sql_mode=oracle;
```
and
```
set sql_mode=default;
```
and change mode accordingly.

**NOTE** If `set sql_mode=oracle;` is encountered, then MaxScale will also
behave as if `autocommit` had been turned off and conversely, if
`set sql_mode=default;` is encountered, then MaxScale will also behave
as if `autocommit` had been turned on.

Note that MariaDB MaxScale is **not** explicitly aware of the sql mode of
the server, so the value of `sql_mode` should reflect the sql mode used
when the server is started.

### `local_address`

What specific local address/interface to use when connecting to servers.

This can be used for ensuring that MaxScale uses a particular interface
when connecting to servers, in case the computer MaxScale is running on
has multiple interfaces.
```
local_address=192.168.1.254
```
If given as a hostname, MaxScale will perform name lookup on the address
when starting and reuse the result.

### `users_refresh_time`

How often, in seconds, MaxScale at most may refresh the users from the
backend server.

MaxScale will at startup load the users from the backend server, but if
the authentication of a user fails, MaxScale assumes it is because a new
user has been created and will thus refresh the users. By default, MaxScale
will do that at most once per 30 seconds and with this configuration option
that can be changed. A value of 0 allows infinite refreshes and a negative
value disables the refreshing entirely.

```
users_refresh_time=120s
```

The value is specified as documented [here](#durations). If no explicit unit
is provided, the value is interpreted as seconds in MaxScale 2.4. In subsequent
versions a value without a unit may be rejected. Note that since the granularity
of the timeout is seconds, a timeout specified in milliseconds will be rejected,
even if the duration is longer than a second.

In MaxScale 2.3.9 and older versions, the minimum allowed value was 10 seconds
but, due to a bug, the default value was 0 which allowed infinite refreshes.

### `users_refresh_interval`

How often, in seconds, MaxScale will automatically refresh the users from the
backend server.

This configuration is used to periodically refresh the backend users, making sure
they are up to date. The default value for this setting is 0, meaning the users
are not periodically refreshed. However, they can still be refreshed in case of
failed authentication depending on `users_refresh_time`.
```
users_refresh_interval=2h
```

The value is specified as documented [here](#durations). If no explicit unit
is provided, the value is interpreted as seconds in MaxScale 2.4.

### `retain_last_statements`

How many statements MaxScale should store for each session. This is for
debugging purposes, as in case of problems it is often of value to be able
to find out exactly what statements were sent before a particular
problem turned up.

**Note:** See also `dump_last_statements` using which the actual dumping
  of the statements is enabled. Unless both of the parameters are defined,
  the statement dumping mechanism doesn't work.

```
retain_last_statements=20
```

Default is `0`.

### `dump_last_statements`

With this configuration item it is specified in what circumstances MaxScale
should dump the last statements that a client sent. The allowed values are
`never`, `on_error` and `on_close`. With `never` the statements are never
logged, with `on_error` they are logged if the client closes the connection
improperly, and with `on_close` they are always logged when a client session
is closed.
```
dump_last_statements=on_error
```
Default is `never`.

Note that you need to specify with `retain_last_statements` how many statements
MaxScale should retain for each session. Unless it has been set to another value
than `0`, this configuration setting will not have an effect.

### `session_trace`

How many log entries are stored in the session specific trace log. This log is
written to disk when a session ends abnormally and can be used for debugging
purposes. Currently the session trace log is written to the log in the following situations:

* When MaxScale receives a fatal signal and is about to crash.
* Whenever an unexpected response is read from a server
* If the session is not closed gracefully (i.e. client doesn't send a  COM_QUIT packet)
* Whenever readwritesplit receives a response that is was not expecting.

It would be good to enable this if a session is disconnected and the log is not
detailed enough. In this case the info log might reveal the true cause of why
the connection was closed.

```
session_trace=20
```
Default is `0`.

The session trace log is also exposed by REST API and is shown with
`maxctrl show sessions`.

The order in which the session trace messages are logged into the log changed in
MaxScale 6.4.9 (MXS-4716). Newer versions will log the messages in the "normal
log order" of older events coming first and newer events appearing later in the
file. Older versions of MaxScale logged the trace dump in the reverse order with
the newest messages first and oldest ones last.

### `session_trace_match`

- **Type**: [regex](#regular-expressions)
- **Default**: none
- **Dynamic**: Yes

If both `session_trace` and `session_trace_match` are defined, and a trace log
entry of a session matches the regular expression, the trace log is written to
disk. The check for the match is done when the session is stopping.

The most effective way to debug MaxScale related issues is to turn on `log_info`
and observe the events written into the MaxScale log. The only problem with this
approach is that it can cause a severe performance bottleneck and can easily
fill up the disk as the amount of data written to it is significant. With
`session_trace` and `session_trace_match`, the content that actually gets logged
can be filtered to only what is needed.

For example, the following configuration would only log the trace log messages
from sessions that execute SQL queries with syntax errors:

```
session_trace=1000
session_trace_match=/You have an error in your SQL syntax/
```

This could be used to easily identify which applications execute the queries
without having to gather the info level log output from all the sessions that
connect to MaxScale. For every session that ends up logging a syntax error
message, the last 1000 lines of log output done by that session is written into
the MaxScale log.

### `writeq_high_water`

High water mark for network write buffer. When the size of the outbound network
buffer in MaxScale for a single connection exceeds this value, network traffic
throtting for that connection is started. The parameter accepts
[size type values](#sizes). The default value is 65536 bytes (was 16777216 bytes
before 22.08.4).

More specifically, if the client side write queue is above this value, it will
block traffic coming from backend servers. If the backend side write queue is
above this value, it will block traffic from client.

The buffer that this parameter controls is the buffer internal to MaxScale and
is not the kernel TCP send buffer. This means that the total amount of buffered
data is determined by both the kernel TCP buffers and the value of
`writeq_high_water`.

Network throttling is only enabled when `writeq_high_water` is non-zero. In
MaxScale 23.02 and earlier, also `writeq_low_water` had to be non-zero.

### `writeq_low_water`

Low water mark for network write buffer. Once the traffic throttling is enabled,
it will only be disabled when the network write buffer is below
`writeq_low_water` bytes. The parameter accepts [size type values](#sizes). The
default value is 1024 bytes (was 8192 bytes before 22.08.4).

The value of `writeq_high_water` must always be greater than the value of
`writeq_low_water`.

### `persist_runtime_changes`

- **Type**: [boolean](#booleans)
- **Default**: true
- **Dynamic**: No

Persist changes done at runtime. This parameter was added in MaxScale 22.08.0.

When `persist_runtime_changes` is enabled, runtime configuration changes done
with the GUI, MaxCtrl or via the REST API cause a new configuration file to be
saved in `/var/lib/maxscale/maxscale.cnf.d/`. If `load_persisted_configs` is
enabled, these files will be applied on top of any existing values found in
static configuration files whenever MaxScale is starting up.

### `load_persisted_configs`

- **Type**: [boolean](#booleans)
- **Default**: true
- **Dynamic**: No

Load persisted runtime changes on startup. This parameter was added in MaxScale
2.3.6.

All runtime configuration changes are persisted in generated configuration files
located by default in `/var/lib/maxscale/maxscale.cnf.d/` and are loaded on
startup after main configuration files have been read. To make runtime
configurations volatile (i.e. they are lost when maxscale is restarted), use
`load_persisted_configs=false`. All changes are still persisted since it stores
the current runtime state of MaxScale. This makes problem analysis easier if an
unexpected outage happens.

### `max_auth_errors_until_block`

The maximum number of authentication failures that are tolerated before a host
is temporarily blocked. The default value is 10 failures. After a host is
blocked, connections from it are rejected for 60 seconds. To disable this
feature, set the value to 0.

Note that the configured value is not a hard limit. The number of tolerated
failures is between `max_auth_errors_until_block` and `threads *
max_auth_errors_until_block` where `max_auth_errors_until_block` is the
configured value of this parameter and `threads` is the number of configured
threads.

### `debug`

Define debug options from the --debug command line option. Either the command
line option or the parameter should be used, not both. The debug options are
only for testing purposes and are not to be used in production.

### REST API Configuration

The MaxScale REST API is an HTTP interface that provides JSON format data
intended to be consumed by monitoring applications and visualization tools.

The following options must be defined under the `[maxscale]` section in the
configuration file.

### `admin_host`

The network interface where the REST API listens on. The default value is the
IPv4 address `127.0.0.1` which only listens for local connections.

### `admin_port`

The port where the REST API listens on. The default value is port 8989.

### `admin_auth`

- **Type**: [boolean](#booleans)
- **Default**: true
- **Dynamic**: No

Enable REST API authentication using HTTP Basic Access
authentication. This is not a secure method of authentication without HTTPS but
it does add a small layer of security.

For more information, read the [REST API documentation](../REST-API/API.md).

### `admin_ssl_key`

The path to the TLS private key in PEM format for the admin interface.

If the `admin_ssl_key` and `admin_ssl_cert` options are all defined, the admin
interface will use encrypted HTTPS instead of plain HTTP.

### `admin_ssl_cert`

The path to the TLS public certificate in PEM format. See `admin_ssl_key`
documentation for more details.

### `admin_ssl_ca_cert`

Deprecated since MariaDB MaxScale 22.08. See `admin_ssl_ca`.

### `admin_ssl_ca`

The path to the TLS CA certificate in PEM format. If defined, the client
certificate, if provided, will be validated against it. This parameter is
optional starting with MaxScale 2.3.19.

**NOTE** Up until MariaDB MaxScale 6, the parameter was called `admin_ssl_ca_cert`,
         which is still accepted as an alias for `admin_ssl_ca`.

### `admin_ssl_version`

Controls the minimum TLS version required to use the REST API.

Accepted values are:

 * TLSv10
 * TLSv11
 * TLSv12
 * TLSv13
 * MAX

The default value is MAX which negotiates the highest level of encryption that
both the client and server support. The list of supported TLS versions depends
on the operating system and what TLS versions the GnuTLS library supports.

This parameter was added in MaxScale 2.5.7.

### `admin_enabled`

- **Type**: [boolean](#booleans)
- **Default**: true
- **Dynamic**: No

Enable or disable the admin interface. This allows the admin interface to
be completely disabled to prevent access to it.

### `admin_gui`

- **Type**: [boolean](#booleans)
- **Default**: true
- **Dynamic**: No

Enable or disable the admin graphical user interface.

MaxScale provides a GUI for administrative operations via the REST API. When the
GUI is enabled, the root REST API resource (i.e. `http://localhost:8989/`) will
serve the GUI. When disabled, the REST API will respond with a 200 OK to the
request. By disabling the GUI, the root resource can be used as a low overhead
health check.

### `admin_secure_gui`

- **Type**: [boolean](#booleans)
- **Default**: true
- **Dynamic**: No

Whether to serve the GUI only over secure HTTPS connections.

To be secure by default, the GUI is only served over HTTPS connections as
it uses a token authentication scheme. This also controls whether the
`/auth` endpoint requires an encrypted connection.

To allow use of the GUI without having to configure TLS certificates for
the MaxScale REST API, set this parameter to false.

### `admin_log_auth_failures`

- **Type**: [boolean](#booleans)
- **Default**: true
- **Dynamic**: Yes

Log authentication failures for the admin interface.

### `admin_pam_readwrite_service` and `admin_pam_readonly_service`

Use Pluggable Authentication Modules (PAM) for REST API authentication. The settings
accept a PAM service name which is used during authentication if normal authentication
fails. `admin_pam_readwrite_service` should accept users who can do any
MaxCtrl/REST-API-operation. `admin_pam_readonly_service` should accept users who can only
do read operations. Because REST-API does not support back and forth communication between
the client and MaxScale, the PAM services must be simple. They should only ask for the
password and nothing else.

If only `admin_pam_readwrite_service` is configured, both read and write operations can be
authenticated by PAM. If only `admin_pam_readonly_service` is configured, only read
operations can be authenticated by PAM. If both are set, the service used is determined by
the requested operation. Leave or set both empty to disable PAM for REST-API.

### `admin_readwrite_hosts`

- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `%`

Limit REST-API logins to specific source addresses/hosts. Supports
a comma-separated list of addresses and hostnames. Addresses can be given in
CIDR-notation. Admin clients still need to supply credentials as usual.
By default, all source addresses are allowed. `admin_readwrite_hosts` lists
the hosts from which any operation is allowed.

```
admin_readwrite_hosts=192.168.1.1,127.0.0.1/21
```

When listing hostnames, `%` and `_` act as wildcards, similar to the hostname
component in MariaDB Server user accounts. `localhost` is a reserved hostname
and will not match any connection (use `127.0.0.1` for loopback connections).

When checking the source host of the incoming REST-API client, MaxScale first
compares against addresses and address masks. If a match was not found and the
setting values contain hostnames, reverse name lookup is performed on the
client address. The lookup can take a while in rare cases. To prevent such
slowdown, use only IP-addresses in the host lists.

`skip_name_resolve` cannot be enabled if `admin_readwrite_hosts` or
`admin_readonly_hosts` includes hostname patterns, as these would not work.

### `admin_readonly_hosts`

Works similar to `admin_readwrite_hosts`. Lists the hosts from which only read
operations are allowed. An admin client can do a read operation if their source
address matches either `admin_readwrite_hosts` or `admin_readonly_hosts`.

```
admin_readonly_hosts=mydomain%.com
```

### `admin_jwt_algorithm`

- **Type**: [enum](#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `auto`, `HS256`, `HS384`, `HS512`, `RS256`, `RS384`, `RS512`, `PS256`, `PS384`, `PS512`, `ES256`, `ES384`, `ES512`, `ED25519`, `ED448`
- **Default**: `auto`

The signature algorithm used by the MaxScale REST API when generating JSON Web
Tokens.

For more information about the tokens and how they work, refer to [the REST API
documentation](../REST-API/API.md).

If a symmetric algorithm is used (i.e. `HS256`, `HS384` or `HS512`), MaxScale
will generate a random encryption key on startup and use that to sign the
messages. The symmetric key can also be retrieved from an [Encryption Key
Manager]() if the `admin_jwt_key` parameter is defined.

If an asymmetric algorithm (i.e. public key authentication) is used, both the
`admin_ssl_cert` and `admin_ssl_key` parameters must be defined and they must
contain a private key and a public certificate of the correct type. If the wrong
key type, key length or elliptic curve is used, MaxScale will refuse to start.

Asymmetric key algorithms make it possible for the clients of the REST API to
validate that the token was indeed generated by the correct entity.

Symmetric algorithms make it easy to share the same tokens between
multiple MaxScale instances as the shared secret can be stored in a key
management system.

The possible values for this parameter are:

* `auto`

  * MaxScale will attempt to detect the best algorithm to use for
    signatures. The algorithm used depends on the private key type: RSA keys use
    `PS256`, EC keys use the `ES256`, `ES384` or `ES512` depending on the curve,
    Ed25519 keys use `ED25519` and Ed448 keys uses `ED448`. If MaxScale cannot
    auto-detect the key type, it falls back to `HS256` as the default algorithm.

* `HS256`, `HS384` or `HS512`

  * [HMAC with SHA-2
    Functions](https://datatracker.ietf.org/doc/html/rfc7518#section-3.2). If
    `admin_jwt_key` is not defined, uses a random encryption key of the correct
    size.

* `RS256`, `RS384` or `RS512`

  * [Digital Signature with
    RSASSA-PKCS1-v1_5](https://datatracker.ietf.org/doc/html/rfc7518#section-3.3). Requires
    at least a 2048-bit RSA key.

* `PS256`, `PS384` or `PS512`

  * [Digital Signature with
    RSASSA-PSS](https://datatracker.ietf.org/doc/html/rfc7518#section-3.5). Requires
    at least a 2048-bit RSA key.

* `ES256`, `ES384` or `ES512`

  * [Digital Signature with
    ECDSA](https://datatracker.ietf.org/doc/html/rfc7518#section-3.4). Requires
    an EC key with the correct curve: P-256 for `ES256`, P-384 for `ES384` and
    P-512 for `ES512`.

* `ED25519` or `ED448`

  * [Edwards-curve Digital Signature Algorithm
    (EdDSA)](https://www.rfc-editor.org/rfc/rfc8037#section-3). Requires a
    Ed25519 key for `ED25519` or a Ed448 key for `ED448`.

### `admin_jwt_key`

- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

The ID for the encryption key used to sign the JSON Web Tokens. If configured,
an [Encryption Key Manager](#encryption-key-managers) must also be configured
and it must contain the key with the given ID. If no key is defined, MaxScale
will use a random encryption key whenever a symmetric signature algorithm is
used.

Currently, the encryption key is only read on startup. This means that the
tokens will be signed by the latest key version that is available on startup:
rotating the encryption key in the key management system will not cause the JWTs
to be signed with newer versions of the key.

### `admin_jwt_max_age`

- **Type**: [duration](#durations)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `24h`

The maximum lifetime of a token generated by the `/auth` endpoint.

If a client requests for a token with a lifetime that exceeds the configured
value, the token lifetime is silently truncated to this value. This can be used
to control the maximum length of a MaxGUI session.

This also acts as the effective maximum age of any database connection created
from the `/sql` endpoint.

### `admin_oidc_url`

- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

The URL to a OpenID Connect server that is used for JWT validation.

If defined, any tokens signed by this server are accepted as valid bearer tokens
for the MaxScale REST API. The `"sub"` field of the token is assumed to be the
username of an administrative user in MaxScale and the `"account"` claim is
assumed to be the type of the user: `"admin"` for administrative users with full
access to the REST-API and `"basic"` for users with read-only access to the
REST-API. This means that all users must be first created with `maxctrl create
user` before the tokens are accepted if the OIDC provider is not able to add the
`"account"` claim.

If this URL is changed at runtime, the new certificates will not be
fetched until a `maxctrl reload tls` command is executed.

### `admin_verify_url`

- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""`

URL to a server to which the REST API token verification is delegated.

If the URL is defined, any tokens passed to the REST API will be validated by
doing a GET request to the URL with the client's token as a bearer token. The
`Referer` header of the request is set to the URL being requested by the client
and the custom `X-Referrer-Method` header is set to the HTTP method being used
(PUT, GET etc.).

**Note**: When `admin_verify_url` is used and the remote server cannot
be accessed, all REST API access that uses tokens will be disabled. The
only way to use the REST API with tokens is to remove `admin_verify_url`
from the configuration which requires restarting MaxScale. The REST API
still accepts HTTP Basic Access authentication even if the remote server
cannot be reached.

By delegating the authentication and authorization of the REST API to an
external server, users can implement custom access control systems for the
MaxScale REST API.

### `admin_jwt_issuer`

- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `maxscale`

The issuer (`"iss"`) claim of all JWTs generated by MaxScale. This can be set
to a custom value to uniquely identify which MaxScale issued a JWT. This is
especially useful for cases where the MaxScale GUI is used from behind
a reverse proxy.

### `admin_audit`

- **Type**: [boolean](#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

Enable logging of incoming REST API calls.

### `admin_audit_file`

- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `admin_audit.csv` in [logdir](#logdir)

If specified, the absolute path must be given, e.g.
`/var/log/maxscale/audit_files/audit.csv`, the directory
`/var/log/maxscale/audit_files` must exist.

### `admin_audit_exclude_methods`

- **Type**: [enum](#enumerations)
- **Mandatory**: No
- **Dynamic**: Yes
- **Values**: `GET`, `PUT`, `POST`, `PATCH`, `DELETE`, `HEAD`, `OPTIONS`, `CONNECT`, `TRACE`
- **Default**: No exclusions

List of comma separated HTTP methods to exclude from logging
Currently MaxScale does not use `CONNECT` or `TRACE`.

Resetting to log all methods can be done in the configuration file by
writing `admin_audit_exclude_methods=` or at runtime with
`maxctrl alter maxscale admin_audit_exclude_methods=`.
Remember that once a runtime change has been made, the entry for that
setting is ignored in the main configuration file (usually maxscale.cnf).

### `config_sync_cluster`

- **Type**: monitor
- **Default**: No default value
- **Dynamic**: Yes

This parameter controls which cluster (i.e. monitor) is used to synchronize
configuration changes between MaxScale instances. The first server labeled
`Master` will be used for the synchronization.

By default configuration synchronization is not enabled and it must be
explicitly enabled by defining a monitor name for `config_sync_cluster`.

When `config_sync_cluster` is defined, `config_sync_user` and
`config_sync_password` must also be defined.

For a detailed description of this feature, refer to the [Configuration
Synchronization](#configuration-synchronization) section.

### `config_sync_user`

- **Type**: string
- **Default**: No default value
- **Dynamic**: Yes

The username for the account that is used to synchronize configuration changes
across MaxScale instances. Both this parameter and `config_sync_password` are
required if `config_sync_cluster` is configured.

This account must have the following grants:

```
GRANT SELECT, INSERT, UPDATE, CREATE ON `mysql`.`maxscale_config`
```

If the database where the table is created is changed with `config_sync_db`, the
grants must be adjusted to target that database instead.

### `config_sync_password`

- **Type**: password
- **Default**: No default value
- **Dynamic**: Yes

The password for `config_sync_user`. Both this parameter and `config_sync_user`
are required if `config_sync_cluster` is configured. This password can
optionally be encrypted using `maxpasswd`.

### `config_sync_db`

- **Type**: string
- **Default**: `mysql`
- **Dynamic**: No

The database where the `maxscale_config` table is created. By default the table
is created in the `mysql` database. This parameter was added in MaxScale
versions 6.4.6 and 22.08.5.

As tables in the `mysql` database cannot have triggers on them, the database
must be changed to a user-created one in order to create triggers on the table.
An example use-case for triggers on this table is to track all configuration
changes done to MaxScale by inserting them into a separate table.

### `config_sync_interval`

- **Type**: [duration](#durations)
- **Default**: 5s
- **Dynamic**: Yes

How often to synchronize the configuration with the cluster.

As the synchronization involves selecting the configuration version from the
database, this value should not be set to an unreasonably low value. The default
value of 5 second should provide a good compromise between responsiveness and
how much load it places on the database.

### `config_sync_timeout`

- **Type**: [duration](#durations)
- **Default**: 10s
- **Dynamic**: Yes

Timeout for all SQL operations done during the configuration synchronization. If
an operation exceeds this timeout, the configuration change is treated as failed
and an error is reported to the client that did the change.

### `key_manager`

- **Type**: [enum](#enumerations)
- **Dynamic**: Yes
- **Values**: `none`, `file`, `kmip`, `vault`
- **Default**: `none`

The encryption key manager to use. The available encryption key managers are:

* `none` - No key manager, encryption keys are not available.

* `file` - [File-based key manager](#file-based-key-manager)

* `kmip` - [KMIP key manager](#kmip-key-manager)

* `vault` - [HashiCorp Vault key manager](#hashicorp-vault-key-manager). This
            key manager is only included on systems with GCC 9 or newer which
            means it cannot be used on Ubuntu 18.04.

Refer to the [Encryption Key Managers](#encryption-key-managers) section for
more information on how to configure the key managers. The key managers each
have their configuration in their own namespace and must have their name as a
prefix.

For example to configure the `file` key manager, the following must be used:

```
key_manager=file
file.keyfile=/path/to/keyfile
```

## Events

MaxScale logs warnings and errors for various reasons and often it is self-
evident and generally applicable whether some occurrence should warrant a
warning or an error, or perhaps just an info-level message.

However, there are events whose seriousness is not self-evident. For
instance, in some environments an authentication failure may simply indicate
that someone has made a typo, while in some other environment that can only
happen in case there has been a security breech.

To handle events like these, MaxScale defines _events_ whose logging
facility and level can be controlled by the administrator. Given an event
`X`, its facility and level are controlled in the following manner:
```
event.X.facility=LOG_LOCAL0
event.X.level=LOG_ERR
```
The above means that if event _X_ occurs, then that is logged using the
facility `LOG_LOCAL0` and the level `LOG_ERR`.

The valid values of facility` are the facility values reported by `man
syslog`, e.g. `LOG_AUTH`, `LOG_LOCAL0` and `LOG_USER`. Likewise, the valid
values for `level` are the ones also reported by `man syslog`,
e.g. `LOG_WARNING`, `LOG_ERR` and `LOG_CRIT`.

Note that MaxScale does not act upon the level, that is, even if the level
of a particular event is defined to be `LOG_EMERG`, MaxScale will not shut
down if that event occurs.

The default facility is `LOG_USER` and the default level is `LOG_WARNING`.

Note that you may also have to configure `rsyslog` to ensure that the
event can be logged to the intended log file. For instance, if the facility
is chosen to be `LOG_AUTH`, then `/etc/rsyslog.conf` should contain a line
like
```
auth,authpriv.*                 /var/log/auth.log
```
for the logged events to end up in `/var/log/auth.log`, where the initial
`auth` is the relevant entry.

The available events are:

### 'authentication_failure'

This event occurs when there is an authentication failure.
```
event.authentication_failure.facility=LOG_AUTH
event.authentication_failure.level=LOG_CRIT
```

## Service

A service represents the database service that MariaDB MaxScale offers to the
clients. In general a service consists of a set of backend database servers and
a routing algorithm that determines how MariaDB MaxScale decides to send
statements or route connections to those backend servers.

A service may be considered as a virtual database server that MariaDB MaxScale
makes available to its clients.

Several different services may be defined using the same set of backend servers.
For example a connection based routing service might be used by clients that
already performed internal read/write splitting, whilst a different statement
based router may be used by clients that are not written with this functionality
in place. Both sets of applications could access the same data in the same
databases.

A service is identified by a service name, which is the name of the
configuration file section and a type parameter of service.

```
[Test-Service]
type=service
```

In order for MariaDB MaxScale to forward any requests it must have at least one
service defined within the configuration file. The definition of a service alone
is not enough to allow MariaDB MaxScale to forward requests however, the service
is merely present to link together the other configuration elements.

### `router`

The router parameter of a service defines the name of the router module that
will be used to implement the routing algorithm between the client of MariaDB
MaxScale and the backend databases. Additionally routers may also be passed a
comma separated list of options that are used to control the behavior of the
routing algorithm. The two parameters that control the routing choice are router
and router_options. The router options are specific to a particular router and
are used to modify the behavior of the router. The read connection router can be
passed options of `master`, `slave` or `synced`, an example of configuring a service
to use this router and limiting the choice of servers to those in `slave` state
would be as follows.

```
router=readconnroute
router_options=slave
```

To change the router to connect on to servers in the `master` state as well as
slave servers, the router options can be modified to include the `master` state.

```
router=readconnroute
router_options=master,slave
```

A more complete description of router options and what is available for a given
router is included with the documentation of the router itself.

### `router_options`

Option string given to the router module. The value of this parameter should be
a comma-separated list of key-value pairs. See router specific documentation for
more details.

### `filters`

The filters option allow a set of filters to be defined for a service; requests
from the client are passed through these filters before being sent to the router
for dispatch to the backend server.  The filters parameter takes one or more
filter names, as defined within the filter definition section of the
configuration file. Multiple filters are separated using the | character.

```
filters=counter | QLA
```

The requests pass through the filters from left to right in the order defined in
the configuration parameter.

### `targets`

The `targets` parameter is a comma separated list of server and/or service names
that comprise the routing targets of the service. This parameter was added in
MaxScale 2.5.0.

```
targets=My-Service,server2
```

This parameter allows nested service configurations to be defined without having
to configure listeners for all services. For example, one use-case is to use
multiple readwritesplit services behind a schemarouter service to have both the
sharding of schemarouter with the high-availability of readwritesplit.

**NOTE:** The `targets` parameter is mutually exclusive with the `cluster` and
  `servers` parameters.

### `servers`

The servers parameter in a service definition provides a comma separated list of
the backend servers that comprise the service. The server names are those used
in the name section of a block with a type parameter of server (see below).

```
servers=server1,server2,server3
```

**NOTE:** The `servers` parameter is mutually exclusive with the `cluster` and
  `targets` parameters.

### `cluster`

The servers the service uses are defined by the monitor specified as value
of this configuration parameter.

```
cluster=TheMonitor

```

**NOTE:** The `cluster` parameter is mutually exclusive with the `servers` and
  `targets` parameters.

### `user` and `password`

These settings define the credentials the service uses to fetch user account
information from backends. The *password* may be either a plain text password or
an [encrypted password](#encrypting-passwords).

```
user=maxscale
password=Mhu87p2D
```

See [MySQL protocol authentication documentation](../Authenticators/Authentication-Modules.md)
for more information (such as required grants) and troubleshooting tips
regarding user account management and client authentication.

From 23.08.0 onwards, MaxScale will remember the previous password when the
password is changed. If the fetching of the user account information fails
using the new password, it will be attempted using the previous one. The purpose
of this change is to make it a smoother operation to change the password of
the service user. The steps are as follows:

   1. `$ maxctrl alter service MyService password=TheNewPassword`
   1. `MariaDB [(none)]> set password for TheServiceUser = password('TheNewPassword');`

Since the old password is remembered and used if the new password does not
work, it is no longer necessary to perform those steps simultaneously.

### `enable_root_user`

- **Type**: [boolean](#booleans)
- **Default**: false
- **Dynamic**: Yes

This parameter controls the ability of the root user to connect to MariaDB
MaxScale and hence onwards to the backend servers via MariaDB MaxScale.

### `localhost_match_wildcard_host`

Deprecated and ignored.

### `version_string`

This parameter sets a custom version string that is sent in the MySQL Handshake
from MariaDB MaxScale to clients.

Example:

```
version_string=10.11.2-MariaDB-RWsplit
```

If not set, MaxScale will attempt to use a version string from the
backend databases by selecting the version string of the database with
the lowest version number. If the selected version is from the MariaDB
10 series, a `5.5.5-` prefix will be added to it similarly to how the
MariaDB 10 series versions added it.

If MaxScale has not been able to connect to a single database and the
versions are unknown, the default value of `5.5.5-10.4.32 <MaxScale
version>-maxscale` is used where `<MaxScale version>` is the version of
MaxScale.

### `auth_all_servers`

- **Type**: [boolean](#booleans)
- **Default**: false
- **Dynamic**: Yes

**Note:** This parameter has been deprecated in MaxScale 24.02. Modules that
  require this to function correctly (e.g. schemarouter) now automatically
  enable it.

This parameter controls whether only a single server or all of the servers are
used when loading the users from the backend servers.

By default MaxScale uses the first server labeled as `Master` as the source of
the authentication data. When this option is enabled, the authentication data is
loaded from all the servers and combined into one big data set.

### `strip_db_esc`

- **Type**: [boolean](#booleans)
- **Default**: true
- **Dynamic**: Yes

**Note:** This parameter has been deprecated in MaxScale 23.08. The stripping of
  escape characters is in all known cases the correct thing to do.

This setting controls whether escape characters (`\`) are removed from database
names when loading user grants from a backend server.  When enabled, a grant
such as ``grant select on `test\_`.* to 'user'@'%';`` is read as ``grant select
on `test_`.* to 'user'@'%';``

This setting has no effect on database-level grants fetched from a MariaDB
Server. The database names of a MariaDB Server are compared using the LIKE
operator to properly handle wildcards and escaped wildcards. This setting may
affect database names in table and column level grants, although these typically
do not contain backlashes.

Some visual database management tools automatically escape some characters and
this might cause conflicts when MaxScale tries to authenticate users.

### `log_auth_warnings`

- **Type**: [boolean](#booleans)
- **Default**: true
- **Dynamic**: Yes

Enable or disable the logging of authentication failures and warnings. If
enabled, messages about failed authentication attempts will be logged with
details about who tried to connect to MariaDB MaxScale and from where.

### `log_warning`

- **Type**: [boolean](#booleans)
- **Default**: false
- **Dynamic**: Yes

When enabled, this allows a service to log warning messages even if the global
log level configuration disables them.

Note that disabling the service level logging does not override the global
logging configuration: with `log_warning=false` in the service and
`log_warning=true` globally, warnings will still be logged for all services.

### `log_notice`

- **Type**: [boolean](#booleans)
- **Default**: false
- **Dynamic**: Yes

When enabled, this allows a service to log notice messages even if the global
log level configuration disables them.

### `log_info`

- **Type**: [boolean](#booleans)
- **Default**: false
- **Dynamic**: Yes

When enabled, this allows a service to log info messages even if the global log
level configuration disables them.

### `log_debug`

- **Type**: [boolean](#booleans)
- **Default**: false
- **Dynamic**: Yes

When enabled, this allows a service to log debug messages even if the global log
level configuration disables them.

Debug messages are only enabled for debug builds. Enabling `log_debug` in a
release build does nothing.

### `wait_timeout`

- **Type**: [duration](#durations)
- **Default**: 0s
- **Mandatory**: No
- **Dynamic**: Yes
- **Auto tune**: [Yes](#auto_tune)

The wait_timeout parameter is used to disconnect sessions to MariaDB
MaxScale that have been idle for too long. The session timeouts are disabled by
default. To enable them, define the timeout in seconds in the service's
configuration section. A value of zero is interpreted as no timeout, the same
as if the parameter is not defined.

This parameter used to be called `connection_timeout` and this name is still
accepted as an alias for `wait_timeout`. The old name has been deprecated in
MaxScale 23.08.

Note that since the granularity of the timeout is seconds, a timeout specified
in milliseconds will be rejected, even if the duration is longer than a second.

This parameter only takes effect in top-level services. A top-level service is
the service where the listener that the client connected to points (i.e. the
value of `service` in the listener). If a service defines other services in its
`targets` parameter, the `wait_timeout` for those is not used.

The value of `wait_timeout` in MaxScale should be lower than the lowest
`wait_timeout` value on the backend servers. This way idle clients are
disconnected by MaxScale before the backend servers have to close them. Any
client-side idle timeouts (e.g. maximum lifetime for connection pools) should be
lower than `wait_timeout` in both MaxScale and MariaDB. This way the client
application will end up closing the connection itself which most of the time
results in better and more helpful error messages.

**Warning:** If a connection is idle for longer than the configured connection
timeout, it will be forcefully disconnected and a warning will be logged in the
MaxScale log file.

Example:

```
[Test-Service]
wait_timeout=300s
```

### `max_connections`

The maximum number of simultaneous connections MaxScale should permit to this
service. If the parameter is zero or is omitted, there is no limit. Any attempt
to make more connections after the limit is reached will result in a "Too many
connections" error being returned.

**Warning**: In MaxScale 2.5, it is possible that the number of concurrent
  connections temporarily exceeds the value of `max_connections`. This has been
  fixed in MaxScale 6.

Example:

```
[Test-Service]
max_connections=100
```

### `session_track_trx_state`

- **Type**: [boolean](#booleans)
- **Default**: false
- **Dynamic**: Yes

**Note:* This parameter has been deprecated in MaxScale 23.08 as the feature is
  now used automatically if needed. In addition, the session tracking no longer
  needs to be enabled in MariaDB for the transaction state tracking to work
  correctly.

Enable transaction state tracking by offloading it to the backend servers.
Getting the transaction state from the server will be more accurate for stored
procedures or multi-statement SQL that modifies the transaction state
non-atomically.

In general, it is better to avoid using this type of SQL as tracking the
transaction state via the server responses is not compatible with features such
as `transaction_replay` in readwritesplit. `session_track_trx_state` should only
be enabled if the default transaction tracking done by MaxScale does not produce
the desired outcome.

This is only supported by MariaDB versions 10.3 or newer. The following must be
configured in the MariaDB server in order for this feature to work. Not
configuring the MariaDB server with it can result in the transaction state being
wrong in MaxScale which can result in data inconsistency.

```
session_track_state_change = ON
session_track_transaction_info = CHARACTERISTICS
```

### `retain_last_statements`

How many statements MaxScale should store for each session of this service.
This overrides the value of the global setting with the same name. If
`retain_last_statements` has been specified in the global section of the
MaxScale configuration file, then if it has _not_ been explicitly specified
for the service, the global value holds, otherwise the service specific
value rules. That is, it is possible to enable the setting globally and
turn it off for a specific service, or just enable it for specific services.

The value of this parameter can be changed at runtime using `maxctrl` and the
new value will take effect for sessions created thereafter.

```
maxctrl alter service MyService retain_last_statements 5
```

### `connection_keepalive`

- **Type**: [duration](#durations)
- **Default**: 300s
- **Mandatory**: No
- **Dynamic**: Yes
- **Auto tune**: [Yes](#auto_tune)

Keep idle connections alive by sending pings to backend servers. This feature
was introduced in MaxScale 2.5.0 where it was changed from a
readwritesplit-specific feature to a generic service feature. The default value
for this parameter is 300 seconds. To disable this feature, set the value to 0.

The keepalive interval is specified as documented [here](#durations). If no
explicit unit is provided, the value is interpreted as seconds in MaxScale
2.5. In subsequent versions a value without a unit may be rejected. Note that
since the granularity of the keepalive is seconds, a keepalive specified in
milliseconds will be rejected, even if the duration is longer than a second.

The parameter value is the interval in seconds between each keepalive ping. A
keepalive ping will be sent to a backend server if the connection has been idle
for longer than the configured keepalive interval.

Starting with MaxScale 2.5.21 and 6.4.0, the keepalive pings are not sent if the client
has been idle for longer than the configured value of
`connection_keepalive`. Older versions of MaxScale sent the keepalive pings
regardless of the client state.

This parameter only takes effect in top-level services. A top-level service is
the service where the listener that the client connected to points (i.e. the
value of `service` in the listener). If a service defines other services in its
`targets` parameter, the `connection_keepalive` for those is not used.

If the value of `connection_keepalive` is changed at runtime, the change in the
value takes effect immediately.

As the connection keepalive pings must be done only when there's no ongoing
query, all requests and responses must be tracked by MaxScale. In the case of
`readconnroute`, this will incur a small drop in performance. For routers that
rely on result tracking (e.g. `readwritesplit` and `schemarouter`), the
performance will be the same with or without `connection_keepalive`.

If you want to avoid the performance cost and you don't need the connection
keepalive feature, you can disable it with `connection_keepalive=0s`.

### `force_connection_keepalive`

- **Type**: boolean
- **Default**: false
- **Dynamic**: Yes

By default, connection keepalive pings are only sent if the client is either
executing a query or has been idle for less than the duration configured in
`connection_keepalive`. When this parameter is enabled, keepalive pings are
unconditionally sent to any backends that have been idle for longer than
`connection_keepalive` seconds. This option was added in MaxScale 6.4.9 and can
be used to emulate the pre-2.5.21 behavior if long-lived application connections
rely on the old unconditional keepalive pings.

*Note:* if `force_connection_keepalive` is enabled and `connection_keepalive` in
MaxScale is set to a lower value than the `wait_timeout` on the database, the
client idle timeouts that `wait_timeout` control are no longer effective. This
happens because MaxScale unconditionally sends the pings which make the client
behave like it is not idle and thus the connections will never be killed due to
`wait_timeout`.

### `net_write_timeout`

This parameter controls how long a network write to the client can stay
buffered. This feature is disabled by default.

When `net_write_timeout` is configured and data is buffered on the client
network connection, if the time since the last successful network write exceeds
the configured limit, the client connection will be disconnected.

The value is specified as documented [here](#durations). If no explicit unit
is provided, the value is interpreted as seconds in MaxScale 2.4. In subsequent
versions a value without a unit may be rejected. Note that since the granularity
of the timeout is seconds, a timeout specified in milliseconds will be rejected,
even if the duration is longer than a second.

### `max_sescmd_history`

- **Type**: integer
- **Default**: 50
- **Dynamic**: Yes

`max_sescmd_history` sets a limit on how many distinct session commands are
stored in the session command history. When the history limit is exceeded, the
history is either pruned to the last `max_sescmd_history` command (when
`prune_sescmd_history` is enabled) or the history is disabled and server
reconnections are no longer possible.

The required history size can be estimated by counting the total number of
prepared statements and session state modifying commands (e.g `SET NAMES`) that
are used by a client. Note that connectors usually add some commands that aren't
visible to the application developer which means a safety margin should be
added. A good rule of thumb is to count the expected number of statements and
double that number.

This parameter was moved into the MaxScale core in MaxScale 6.0. The parameter
can be configured for all routers that support the session command
history. Currently only `readwritesplit` and `schemarouter` support it.

### `prune_sescmd_history`

- **Type**: [boolean](#booleans)
- **Default**: true
- **Dynamic**: Yes

This option enables pruning of the session command history when it exceeds the
value configured in `max_sescmd_history`. When this option is enabled, only a
set number of statements are stored in the history. This limits the per-session
memory use while still allowing safe reconnections.

This parameter is intended to be used with pooled connections that remain in use
for a very long time. Most connection pool implementations do not reset the
session state and instead re-initialize it with new values. This causes the
session command history to grow at roughly a constant rate for the lifetime of
the pooled connection.

Starting with MaxScale 23.08, the session command history is also simplified
before being stored. The simplification is done by removing repeated occurrences
of the same command and only executing the latest one of them. The order in
which the commands are executed still remains the same but inter-dependencies
between commands are not preserved.

For example, the following set of commands demonstrates how the history
simplification works and how inter-dependencies can be lost.

```sql
SET @my_planet='Earth';                            -- This command will be removed by history simplification
SET @my_home='My home is: ' || @my_planet;         -- Command #1 in the history
SET @my_planet='Earth';                            -- Command #2 in the history
```

In the example, the value of `@my_home` has a dependency on the value of
`@my_planet` which is lost when the same statement is executed again and
the history simplification removes the earlier one.

This same problem can occur even in older versions of MaxScale that used
a sliding window of the history when the window moves past the statement
that later statement depended on. If inter-dependent session commands
are being used, the history pruning should be disabled.

Each client-side session that uses a pooled connection only executes a finite
amount of session commands. By retaining a shorter history that encompasses all
session commands the individual clients execute, the session state of a pooled
connection can be accurately recreated on another server.

When the session command history pruning is enabled, there is a theoretical
possibility that upon server reconnection the session states of the connections
are inconsistent. This can only happen if the length of the stored history is
shorter than the list of relevant statements that affect the session state. In
practice the default value of 50 session commands is a fairly reasonable value
and the risk of inconsistent session state is relatively low.

In case the default history length is too short for safe pruning, set the value
of `max_sescmd_history` to the total number of commands that affect the session
state plus a safety margin of 10. The safety margin reserves some extra space
for new commands that might be executed due to changes in the client side
application.

This parameter was moved into the MaxScale core in MaxScale 6.0. The parameter
can be configured for all routers that support the session command
history. Currently only `readwritesplit` and `schemarouter` support it.

### `disable_sescmd_history`

- **Type**: [boolean](#booleans)
- **Default**: false
- **Dynamic**: Yes

This option disables the session command history. This way no history is stored
and if a replica server fails, the router will not try to replace the failed
replica. Disabling session command history will allow long-lived connections
without causing a constant growth in the memory consumption.

This parameter should only be used when either the memory footprint must be as
small as possible or when the pruning of the session command history is not
acceptable.

This parameter was moved into the MaxScale core in MaxScale 6.0. The parameter
can be configured for all routers that support the session command
history. Currently only `readwritesplit` and `schemarouter` support it.

### `user_accounts_file`

Defines path to a file with additional user accounts for incoming clients.
Default value is empty, which disables the feature.
```
user_accounts_file=/home/root/users.json
```

In addition to querying the backends, MaxScale can read users from a file. This
feature is useful when backends have limitations on the type of users that can
be created, or if MaxScale needs to allow users to log in even
when backends are down (e.g. binlog router). The users read from the file are
only present on MaxScale, so logging into backends can still fail. The format of
the file is protocol-specific. The following only applies to MariaDB-protocol,
which is also the only protocol supporting this feature.

The file contains json text. Three objects are read from it: *user*, *db* and
*roles_mapping*, none of which are mandatory. These objects must be arrays which
contain user information similar to the *mysql.user*, *mysql.db* and
*mysql.roles_mapping* tables on the server. Each array element must define at
least the string fields *user* and *host*, which define the user account to add
or modify.

The elements in the *user*-array may contain the following additional fields. If
a field is not defined, it is assumed either empty (string) or false (boolean).

- *password*: String. Password hash, similar to the equivalent column on server.
- *plugin*: String. Authentication plugin used by client, similar to server.
- *authentication_string*: String. Additional authentication info, similar to
server.
- *default_role*: String. Default role of user, similar to server.
- *super_priv*: Boolean. True if user has SUPER grant.
- *global_db_priv*: Boolean. True if user can access any database on login.
- *proxy_priv*: Boolean. True if user has a PROXY grant.
- *is_role*: Boolean. True if user is a role.

The elements in the *db*-array must contain the following additional field:

- *db*: String. Database which the user can access. Can contain % and _
wildcards.

The elements in the *roles_mapping*-array must contain the following additional
field:

- *role*: String. Role the user can access.

When users are read from both servers and the file, the server takes priority.
That is, if user `'joe'@'%'` is defined on both, the file-version is discarded.
The file can still affect the database grants and roles of `'joe'@'%'`, as the
*db* and *roles_mapping*-arrays are read separately and added to existing grant
and role lists.

An example users file is below.
```
{
    "user": [
        {
            "user": "test1",
            "host": "%",
            "global_db_priv": true
        },
        {
            "user": "test2",
            "host": "127.0.0.1",
            "password": "*032169CDF0B90AF8C00992D43D354E29A2EACB42",
            "plugin": "mysql_native_password",
            "default_role": "role2"
        },
        {
            "user": "",
            "host": "%",
            "plugin": "pam",
            "proxy_priv": true
        }
    ],
    "db": [
        {
            "user": "test2",
            "host": "127.0.0.1",
            "db": "test"
        }
    ],
    "roles_mapping": [
        {
            "user": "test2",
            "host": "127.0.0.1",
            "role": "role2"
        }
    ]
}
```

### `user_accounts_file_usage`

Defines when *user_accounts_file* is read. The value is an enum, either
"add_when_load_ok" (default) or "file_only_always".

"add_when_load_ok" means that the file is only read when users are successfully
read from a server. The file contents are then added to the server-based data.
If reading from server fails (e.g. servers are down), the file is ignored.

"file_only_always" means that users are not read from the servers at all and the
file contents is all that matters. The state of the servers is ignored. This
mode can be useful with the binlog router, as it allows clients to log in and
fetch binary logs from MaxScale even when backend servers are down.

```
user_accounts_file_usage=file_only_always
```

### `idle_session_pool_time`

Time, default: -1s. Normally, MaxScale only pools backend connections when
a session is closed (controlled by server settings *persistpoolmax* and
*persistmaxtime*). Other sessions can use the pooled connections
instead of creating new connections to backends. If connection sharing is enabled,
MaxScale can pool backend connections also from running sessions, and
re-attach a pooled connection when a session is doing a query. This effectively
allows multiple sessions to share backend connections.

*idle_session_pool_time* defines the amount of time a session must be idle
before its backend connections may be pooled. To enable connection sharing, set
*idle_session_pool_time* to zero or greater. The value can be given in
seconds or milliseconds.

This feature has a significant drawback: when a backend connection is reused, it
needs to be restored to the correct state. This means reauthenticating and
replaying session commands. This can add a significant delay before the
connection is actually ready for a query. If the session command history size
exceeds the value of *max_sescmd_history*, connection sharing is disabled for
the session.

This feature should only be used when limiting the backend connection count is
a priority, even at the cost of query delay and throughput. This feature only
works when the following server settings are also set in MaxScale configuration:

1. [max_routing_connections](#max_routing_connections)
2. [persistpoolmax](#persistpoolmax)
3. [persistmaxtime](#persistmaxtime)

Since reusing a backend connection is an expensive operation, MaxScale only
pools connections when another session requires them. *idle_session_pool_time*
thus effectively limits the frequency at which a connection can be moved from
one session to another. Setting `idle_session_pool_time=0ms` causes MaxScale to
move connections as soon as possible.

```
idle_session_pool_time=900ms
```
See below for more information on configuring connection sharing.

#### Details, limitations and suggestions for connection sharing

As noted above, when a connection is pooled and reused its state is lost.
Although session variables and prepared statements are restored by replaying
session commands, some state information cannot be transferred.

The most common such state is a transaction. When a transaction is on,
connection sharing is disabled for that session until the transaction completes.
Other similar situations may not be properly detected, and it's the
responsibility of the user to avoid introducing such state to the session when
using connection sharing. This means that the following should not be used:

* Statements such as `LOCK TABLES` and `GET LOCK` or any other statement that
  introduces state into the connection.

* Temporary tables and some problematic user or session variables such as
 `LAST_INSERT_ID()`. For `LAST_INSERT_ID()`, the value returned by the connector
  must be used instead of the variable.

* Stored procedures that cause session level side-effects.

Several settings affect connection sharing and its effectiveness. Reusing a
connection is an expensive operation so its frequency should be minimized. The
important configuration settings in addition to *idle_session_pool_time* are
MaxScale server settings
[persistpoolmax](#persistpoolmax),
[persistmaxtime](#persistmaxtime) and
[max_routing_connections](#max_routing_connections).
The service settings [max_sescmd_history](#max_sescmd_history),
[prune_sescmd_history](#prune_sescmd_history) and
[multiplex_timeout](#multiplex_timeout) also have an effect. These
settings should be tuned according to the use case.

*persistpoolmax* limits how many connections can be kept in a pool for a given
server. If the pool is full, no more connections are detached from sessions even
if they are idle and required. The pool size should be large enough to contain
any connections being transferred between sessions, but not be greater than
*max_routing_connections*. Using the value of *max_routing_connections* is a
reasonable starting point.

*persistmaxtime* limits the time a connection may stay in the pool. This should
be high enough so that pooled connections are not unnecessarily closed. Cleaning
up clearly unneeded connections from the pool may be useful when
*max_routing_connections* is restrictively tuned. Because each MaxScale routing
thread has its own connection pool, one thread can monopolize access to a
server. For example, if the pool of thread 1 has 100 connections to *ServerA*
with `max_routing_connections=100`, other threads can no longer connect to the
server. In such a situation, reducing *persistmaxtime* of *ServerA* may help as
it would cause unneeded connections in the pool to be closed faster. Such
connection slots then become available to other routing threads. Reducing the
number of [routing threads](#threads) may also help, as it reduces pool
fragmentation. This may reduce overall throughput, though. When using connection
sharing, backend connections are only in the pool momentarily. Consequently,
*persistmaxtime* can be set quite low, e.g. 10s.

If a client session exceeds *max_sescmd_history* (default 50), pooling is
disabled for that session. If many sessions do this and
*max_routing_connections* is set, other sessions will stall as they cannot find
a backend connection. This can be avoided with *prune_sescmd_history*. However,
pruning means that old session commands will not be replayed when a pooled
connection is reused. If the pruned commands are important
(e.g. statement preparations), the session may fail later on.

If the number of clients actively running queries is greater than
*max_routing_connections*, query throughput will suffer as clients will need to
take turns. In this situation, it's imperative to minimize the number of
backend connections a single session uses. The settings to achieve this depend
on the router. For ReadWriteSplit the following should be used:
```
max_slave_connections=1
lazy_connect=1
transaction_replay=true
```
The above settings mean that MaxScale can process roughly
(*number of replica servers* X *max_routing_connections*) read queries
simultaneously. Write queries will still need to take turns as there is only one
primary server.

The following configuration snippet shows example server and service
configurations for connection sharing with ReadWriteSplit.
```
[server1]
type=server
max_routing_connections=1000 #this should be based on MariaDB Server capacity
persistpoolmax=1000 #same as above
persistmaxtime=10
#other server settings...

[myservice]
type=service
max_slave_connections=1
transaction_replay=true
idle_session_pool_time=500ms
lazy_connect=1
#other service settings...
```

### `multiplex_timeout`

Time, default: 60s. When connection sharing (as described above) is on, clients
may have to wait for their turn to use a backend connection. If too much time
passes without a connection becoming available, MaxScale returns an error to
the client, usually also ending the session. *multiplex_timeout* sets this
timeout. Increase it if queries are failing with "Timed out when waiting for a
connection". Decrease it if failing early is preferable to stalling.
```
multiplex_timeout=33s
```

## Server

Server sections define the backend database servers MaxScale uses. A server is
identified by its section name in the configuration file. The only mandatory
parameter of a server is *type*, but *address* and *port* are also usually
defined. A server may be a member of one or more services. A server may only be
monitored by at most one monitor.

```
[MyMariaDBServer1]
type=server
address=127.0.0.1
port=3000
```

### `address`

The IP-address or hostname of the machine running the database server. MaxScale
uses this address to connect to the server. This parameter is mandatory unless
*socket* is defined. If the address is given as a hostname, MaxScale will
perform name lookup on the hostname when starting and update the result every
minute and when the address changes.

### `port`

The port the backend server listens on for incoming connections. MaxScale uses
this port to connect to the server. The default value is 3306.

### `socket`

The absolute path to a UNIX domain socket the MariaDB server is listening
on. Either *address* or *socket* must be defined and defining them both is an
error.

### `private_address`

Alternative IP-address or hostname for the server. This is currently only used
by MariaDB Monitor to detect and set up replication. See
[MariaDB Monitor documentation](../Monitors/MariaDB-Monitor.md#private_address)
for more information.

### `monitoruser` and `monitorpw`

These settings define a server-specific username and password for monitoring the
server. Monitors typically use the credentials in their own configuration
sections to connect to all servers. If server-specific settings are given, the
monitor uses those instead.

```
monitoruser=mymonitoruser
monitorpw=mymonitorpasswd
```

`monitorpw` may be either a plain text password or an encrypted password.  See
the section [encrypting passwords](#encrypting-passwords) for more information.

### `extra_port`

An alternative port used for administrative connections to the server.  If this
setting is defined, MaxScale uses it for monitoring the server and to fetch user
accounts. Client sessions will still use the normal port.

Defining *extra_port* allows MaxScale to connect even when *max_connections* on
the backend server has been reached. Extra-port connections have their own
connection limit, which is one by default. This needs to be increased to allow
both monitor and user account manager to connect.

If the connection to the extra-port fails due to connection number limit or if
the port is not open on the server, normal port is used.

For more information, see
[extra_port](https://mariadb.com/kb/en/library/thread-pool-system-and-status-variables/#extra_port)
and [extra_max_connections](https://mariadb.com/kb/en/thread-pool-system-status-variables/#extra_max_connections).

### `persistpoolmax`


- **Type**: integer
- **Default**: 0
- **Dynamic**: Yes

Sets the size of the server connection pool. Disabled by default. When enabled,
MaxScale places unused connections to the server to a pool and reuses them
later. Connections typically become unused when a session closes. If the size of
the pool reaches *persistpoolmax*, unused connections are closed instead.

Every routing thread has its own pool. As of version 6.3.0, MaxScale will round
up *persistpoolmax* so that every thread has an equal size pool.

When a MariaDB-protocol connection is taken from the pool to be used in a new
session, the state of the connection is dependent on the router. ReadWriteSplit
restores the connection to match the session state. Other routers do not.

### `persistmaxtime`

The `persistmaxtime` parameter defaults to zero but can be set to a duration as
documented [here](#durations). If no explicit unit is provided, the value is
interpreted as seconds in MaxScale 2.4. In subsequent versions a value without a
unit may be rejected. Note that since the granularity of the parameter is
seconds, a value specified in milliseconds will be rejected, even if the
duration is longer than a second.

A DCB placed in the persistent pool for a server will only be reused if the
elapsed time since it joined the pool is less than the given value. Otherwise,
the DCB will be discarded and the connection closed.

### `max_routing_connections`

Maximum number of routing connections to this server. Connections held in a pool
also count towards this maximum. Does not limit monitor connections or user
account fetching. A value of 0 (default) means no limit.

Since every client session can generate a connection to a server, the server may
run out of memory when the number of clients is high enough. This setting limits
server memory use caused by MaxScale. The effect depends on if the service
setting [idle_session_pool_time](#idle_session_pool_time), i.e. connection
sharing, is enabled or not.

If connection sharing is not on, *max_routing_connections* simply sets a limit.
Any sessions attempting to exceed this limit will fail to connect to the
backend. The client can still connect to MaxScale, but queries will fail.

If connection sharing is on, sessions exceeding the limit will be put on hold
until a connection is available. Such sessions will appear unresponsive, as
queries will hang, possibly for a long time. The timeout is controlled by
[multiplex_timeout](#multiplex_timeout).

```
max_routing_connections=1234
```

### `proxy_protocol`

- **Type**: [boolean](#booleans)
- **Default**: false
- **Dynamic**: Yes

If `proxy_protocol` is enabled, MaxScale will send a
[PROXY protocol](http://www.haproxy.org/download/1.8/doc/proxy-protocol.txt)
header when connecting client sessions to the server. The header contains the
original client IP address and port, as seen by MaxScale. The server will then
read the header and perform authentication as if the connection originated from
this address instead of MaxScale's IP address. With this feature, the user
accounts on the backend server can be simplified to only contain the actual
client hosts and not the MaxScale host.

PROXY protocol will be supported by MariaDB 10.3, which this feature has been
tested with. To use it, enable the PROXY protocol in MaxScale for every
compatible server and configure the MariaDB servers themselves to accept the
protocol headers from MaxScale's IP address. On the server side, the protocol
should be enabled  only for trusted IPs, as it allows the sender to spoof the
connection origin. If a proxy header is sent to a server not expecting it, the
connection will fail. Usually PROXY protocol should be enabled for every
server in a cluster, as they typically have similar grants.

Other SQL-servers may support PROXY protocol as well, but the implementation may
be highly restricting. Strict adherence to the protocol requires that the
backend server does not allow mixing of un-proxied and proxied connections from
a given IP. MaxScale requires normal connections to backends for monitoring and
authentication data queries, which would be blocked. To bypass this restriction,
the server monitor needs to be disabled and the service listener needs to be
configured to disregard authentication errors (`skip_authentication=true`).
Server states also need to be set manually in MaxCtrl. These steps are *not*
required for MariaDB 10.3, since its implementation is more flexible and allows
both PROXY-headered and headerless connections from a proxy-enabled IP.

### `disk_space_threshold`

This parameter specifies how full a disk may be, before MaxScale should start
logging warnings or take other actions (e.g. perform a switchover). This
functionality will only work with MariaDB server versions 10.1.32, 10.2.14 and
10.3.6 onwards, if the `DISKS` _information schema plugin_ has been installed.

**NOTE**: Since MariaDB 10.4.7, MariaDB 10.3.17 and MariaDB 10.2.26, the
information will be available _only_ if the monitor user has the `FILE`
privilege.

A limit is specified as a path followed by a colon and a percentage specifying
how full the corresponding disk may be, before action is taken. E.g. an entry like
```
/data:80
```
specifies that the disk that has been mounted on `/data` may be used until 80%
of the total space has been consumed. Multiple entries can be specified by
separating them by a comma. If the path is specified using `*`, then the limit
applies to all disks. However, the value of `*` is only applied if there is not
an exact match.

Note that if a particular disk has been mounted on several paths, only one path
need to be specified. If several are specified, then the one with the smallest
percentage will be applied.

Examples:
```
disk_space_threshold=*:80
disk_space_threshold=/data:80
disk_space_threshold=/data1:80,/data2:60,*:90
```
The last line means that the disk mounted at `/data1` may be used up to
80%, the disk mounted at `/data2` may be used up to 60% and all other disks
mounted at any paths may be used up until 90% of maximum capacity, before
MaxScale starts to warn to take action.

Note that the path to be used, is one of the paths returned by:
```
> use information_schema;
> select * from disks;
+-----------+----------------------+-----------+----------+-----------+
| Disk      | Path                 | Total     | Used     | Available |
+-----------+----------------------+-----------+----------+-----------+
| /dev/sda3 | /                    |  47929956 | 34332348 |  11139820 |
| /dev/sdb1 | /data                | 961301832 |    83764 | 912363644 |
...
```

There is no default value, but this parameter must be explicitly specified
if the disk space situation should be monitored.

### `rank`

This parameter controls the order in which servers are used. Valid values for
this parameter are `primary` and `secondary`. The default value is
`primary`.

This behavior depends on the router implementation but the general rule of thumb
is that primary servers will be used before secondary servers.

Readconnroute will always use primary servers before secondary servers as long
as they match the configured server type.

Readwritesplit will pick servers that have the same rank as the current
primary. Read the
[readwritesplit documentation on server ranks](../Routers/ReadWriteSplit.md#server-ranks)
for a detailed description of the behavior.

The following example server configuration demonstrates how `rank` can be used
to exclude `DR-site` servers from routing.

```
[main-site-primary]
type=server
address=192.168.0.11
rank=primary

[main-site-replica]
type=server
address=192.168.0.12
rank=primary

[DR-site-primary]
type=server
address=192.168.0.21
rank=secondary

[DR-site-replica]
type=server
address=192.168.0.22
rank=secondary
```

The `main-site-primary` and `main-site-replica` servers will be used as long as
they are available. When they are no longer available, the `DR-site-primary` and
`DR-site-replica` will be used.

### `priority`

- **Type**: integer
- **Default**: 0
- **Dynamic**: Yes

Server priority. Currently only used by galeramon to choose the order in which
nodes are selected as the current primary server. Refer to the
[Server Priorities](../Monitors/Galera-Monitor.md#interaction-with-server-priorities)
section of the galeramon documentation for more information on how to use it.

Starting with MaxScale 2.5.21, this parameter also accepts negative values. In
older versions, the parameter only accepted non-negative values.

### `replication_custom_options`

- **Type**: string
- **Default**: None
- **Dynamic**: Yes

Server-specific custom string added to "CHANGE MASTER TO"-commands sent by
MariaDB Monitor. Overrides `replication_custom_options` setting set in
the monitor. This setting affects the server where the command is ran at, not
the source of the replication. That is, if monitor sends a "CHANGE MASTER TO"-
command to server A telling it to replicate from server B, the setting value
from MaxScale configuration for server A would be used.

See [MariaDB Monitor documentation](../Monitors/MariaDB-Monitor.md#replication_custom_options)
for more information.

## Monitor

Monitor sections are used to define the monitoring module that watches a set of
servers. Each server can only be monitored by one monitor.

Common monitor parameters [can be found here](../Monitors/Monitor-Common.md).

## Listener

A listener defines a port MaxScale listens on for incoming connections. Accepted
connections are linked with a MaxScale service. Multiple listeners can feed the
same service. Mandatory parameters are *type*, *service* and *protocol*.
*address* is optional, it limits connections to a certain network interface
only. *socket* is also optional and is used for Unix socket connections.

The network socket where the listener listens may have a backlog of
connections. The size of this backlog is controlled by the
*net.ipv4.tcp_max_syn_backlog* and *net.core.somaxconn* kernel parameters.

Increasing the size of the backlog by modifying the kernel parameters helps with
sudden connection spikes and rejected connections. For more information see
[listen(2)](http://man7.org/linux/man-pages/man2/listen.2.html).

```
[MyListener1]
type=listener
service=MyService1
protocol=MariaDB
port=3006
```

### `service`

The service to which the listener is associated. This is the name of a service
that is defined elsewhere in the configuration file.

### `protocol`

The name of the protocol module used for communication between the client and
MaxScale. The same protocol is also used for backend communication. Usually this
is set to "mariadb". Other allowed values are "postgresql" and "nosqlprotocol".

### `address`

This sets the address the listening socket is bound to. The address may be
specified as an IP address in 'dot notation' or as a hostname. If left undefined
the listener will bind to all network interfaces.

### `port`

The port the listener listens on. If left undefined a default port for the
protocol is used.

### `socket`

If defined, the listener uses Unix domain sockets to listen for incoming
connections. The parameter value is the name of the socket to use.

If you want to use both network ports and UNIX domain sockets with a service,
define two separate listeners that connect to the same service.

### `authenticator`

The authenticator module to use. Each protocol module defines a default
authentication module, which is used if the setting is left undefined.
MariaDB and PostgreSQL protocols support multiple authenticators and they can
be used simultaneously by giving a comma-separated list e.g.
`authenticator=PAMAuth,mariadbauth,gssapiauth`

### `authenticator_options`

Defines additional options for authentication. The value should be a
comma-separated list of key-value pairs. See protocol and authenticator specific
documentation for more details.

### `sql_mode`

Specify the sql mode for the listener similarly to global `sql_mode` setting.
If both are used this setting will override the global setting for this listener.

### `proxy_protocol_networks`

Define an IP-address or a subnetwork which may send a
[proxy protocol header](http://www.haproxy.org/download/1.8/doc/proxy-protocol.txt)
when connecting. The proxy header contains the original client IP-address and
port, and MaxScale will use that information in its internal bookkeeping.
This means the client is authenticated as if it was connecting from the host
in the proxy header. If proxy protocol is also enabled in MaxScale server
settings, MaxScale will relay the original client address and port to
the server. See [server settings](#proxy_protocol) for more information.

This setting may be useful if a compatible load balancer is relaying client
connections to MaxScale. If proxy headers are used, both MaxScale and the
backends will know where the client originally came from.

The `proxy_protocol_networks`-setting works similarly to the equivalent setting
in [MariaDB Server](https://mariadb.com/kb/en/proxy-protocol-support/).
The value can be a single IP or subnetwork, or a comma-separated list of them.
Subnetworks are given in CIDR-format, e.g. "192.168.0.0/16". "*" is a valid
value, allowing anyone to send the header. "localhost" allows proxy headers
from domain socket connections.

Only trusted IPs should be added to the list, as the proxy header may affect
authentication results.
```
proxy_protocol_networks=192.168.0.1,198.168.0.0/16
```
Similar to MariaDB Server, MaxScale will also accept normal connections even
if `proxy_protocol_networks` is configured for the listener.

### `connection_init_sql_file`

Path to a text file with sql queries. Any sessions created from the listener
will send the contents of the file to backends after authentication. Each
non-empty line in the file is interpreted as a query. Each query must succeed
for the backend connection to be usable for client queries. The queries should
not return any data.

```
connection_init_sql_file=/home/dba/init_queries.txt
```
Example query file:
```
set @myvar = 'mytext';
set @myvar2 = 4;
```

### `user_mapping_file`

Path to a json-text file with user and group mapping, as well as server
credentials. Only affects MariaDB-protocol based listeners. Default value is
empty, which disables the feature.
```
user_mapping_file=/home/root/mapping.json
```
Should not be used together with
[PAM Authenticator](../Authenticators/PAM-Authenticator.md)
settings `pam_backend_mapping` or `pam_mapped_pw_file`, as these may overwrite
the mapped credentials. Is most powerful when combined with service setting
`user_accounts_file`, as then MaxScale can accept users that do not exist on
backends and map them to backend users.

This file functions very similar to
[PAM-based mapping](https://mariadb.com/kb/en/user-and-group-mapping-with-pam/).
Both user-to-user and group-to-user mappings can be defined. Also, the password
and authentication plugin for the mapped users can be added. The file is only
read during listener creation (typically MaxScale start) or when a listener is
modified during runtime. When a client logs into MaxScale, their username is
searched from the mapping data. If the name matches either a name mapping or a
Linux group mapping, the username is replaced by the mapped name. The mapped
name is then used when logging into backends. If the file also contains
credentials for the mapped user, then those are used. Otherwise, MaxScale tries
to log in with an empty password and default MariaDB authentication.

Three arrays are read from the file: *user_map*, *group_map* and
*server_credentials*, none of which are mandatory.

Each array element in the *user_map*-array must define the following fields:

- *original_user*: String. Incoming client username.
- *mapped_user*: String. Username the client is mapped to.

Each array element in the *group_map*-array must define the following fields:

- *original_group*: String. Incoming client Linux group.
- *mapped_user*: String. Username the client is mapped to.

Each array element in the *server_credentials*-array can define the following
fields:

- *mapped_user*: String. The mapped username this password is for.
- *password*: String. Backend server password. Can be encrypted with
*maxpasswd*.
- *plugin*: String, optional. Authentication plugin to use. Must be enabled on
the listener. Defaults to empty, which results in standard MariaDB
authentication.

When a client successfully logs into MaxScale, MaxScale first searches for
name-based mapping. The incoming client does not need to be a Linux user for
name-based mapping to take place. If the name is not found, MaxScale checks if
the client is a Linux user with a group membership matching an element in the
group mapping array. If the client is a member of more than 100 groups, this
check may fail.

If a mapping is found, MaxScale searches the credentials array for a matching
username, and uses the password and plugin listed. The plugin need not be the
same as the one the original user used. Currently, "mysql_native_password" and
"pam" are supported as mapped plugins.

An example mapping file is below.
```
{
    "user_map": [
        {
            "original_user": "bob",
            "mapped_user": "janet"
        },
        {
            "original_user": "karen",
            "mapped_user": "janet"
        }
    ],
    "group_map": [
        {
            "original_group": "visitors",
            "mapped_user": "db_user"
        }
    ],
    "server_credentials": [
        {
            "mapped_user": "janet",
            "password": "secret_pw",
            "plugin": "mysql_native_password"
        },
        {
            "mapped_user": "db_user",
            "password": "secret_pw2",
            "plugin": "pam"
        }
    ]
}
```

### `connection_metadata`

- **Type**: stringlist
- **Default**: `character_set_client=auto,character_set_connection=auto,character_set_results=auto,max_allowed_packet=auto,system_time_zone=auto,time_zone=auto,tx_isolation=auto,maxscale=auto`
- **Dynamic**: Yes
- **Mandatory**: No

Metadata that's sent to all connecting clients. The value must be a
comma-separated list of key-value arguments. The keys or values cannot contain
commas in them.

Any values that are set to `auto` will be substituted with the value of the
corresponding MariaDB system variable. Any system variables that do not not
exist or have empty or null values will not be sent to the client. The system
variable values are read from the first `Master` server that's reachable from
the listener's service. If no `Master` server is reachable, the value is read
from the first `Slave` server and if no `Slave` servers are available, from the
first `Running` server. If no running servers are available, the system
variables are not sent.

The exception to this is the `maxscale=auto` value where the `auto` will be
replaced with the MaxScale version string. This is useful for detecting whether
a client is connected to MaxScale. To make MaxScale completely transparent to
the client application, the `maxscale=auto` value can be removed from
`connection_metadata`.

MaxScale will always send a metadata value for `threads_connected` that contains
the current number of connections to the service that the listener points to and
for `connection_id` that contains the 64-bit connection ID value. The values can
be overridden by defining them with some value, for example,
`connection_metadata=threads_connected=0,connection_id=0`.

The metadata is implemented using
[the session state information](https://mariadb.com/kb/en/ok_packet/#session-state-info)
that is embedded in the OK packets that are generated by MaxScale. The values
are encoded as system variables changes. This information can be accessed by all
connectors that support reading the session state information. One example of
this is the MariaDB Connector/C that implements it with the
[mysql_session_track_get_first](https://github.com/mariadb-corporation/mariadb-connector-c/wiki/mysql_session_track_get_first)
and
[mysql_session_track_get_next](https://github.com/mariadb-corporation/mariadb-connector-c/wiki/mysql_session_track_get_next)
functions.

The following example demonstrates the use of `connection_metadata`:

```
connection_metadata=redirect_url=localhost:3306,service_name=my-service,max_allowed_packet=auto
```

The configuration has three variables, `redirect_url`, `service_name` and
`max_allowed_packet` that have the values `localhost:3306`, `my-service` and
`auto`. The `auto` value is special and gets replaced with the
`max_allowed_packet` value from the MariaDB server. This means that the final
metadata that is sent to the client would be `redirect_url=localhost:3306`,
`service_name=my-service` and `max_allowed_packet=16777216`.

### Version-specific Behavior

If the `connection_metadata` variable list contains the `tx_isolation` variable
and the backend MariaDB server from which the variable is retrieved is MariaDB
11 or newer, the value is renamed to `transaction_isolation`. The `tx_isolation`
parameter was deprecated in favor of `transaction_isolation` in MariaDB 11
(MDEV-21921).

## Include

An _include_ section defined common parameters used in other configuration
sections. Consider the following configuration.
```
[Monitor1]
type=monitor
module=mariadbmon
user=the_user
password=the_password
handle_events=false
monitor_interval=2000ms
backend_connect_timeout = 3s
backend_connect_attempts = 5
servers=Server1, Server2

[Monitor2]
type=monitor
module=mariadbmon
user=the_user
password=the_password
handle_events=false
monitor_interval=2000ms
backend_connect_timeout = 3s
backend_connect_attempts = 5
servers=Server3, Server4
```
The two monitor sections are identical except for the `servers` setting.
If they otherwise should remain identical, a change must be made in two
places. With an `include` section the situation can be simplified.

```
[Monitor-Common]
type=include
module=mariadbmon
user=the_user
password=the_password
handle_events=false
monitor_interval=2000ms
backend_connect_timeout = 3s
backend_connect_attempts = 5

[Monitor1]
type=monitor
@include=Monitor-Common
servers=Server1, Server2

[Monitor2]
type=monitor
@include=Monitor-Common
servers=Server3, Server3
```
With an `include` section, all common settings can be defined in one
place, and then included to any number of other sections using the
`@include` parameter.

The `@include` parameter takes a list of section names, so the settings
can be distributed across several `include` sections.
```
@include=Some-Common-Attributes, Other-Common-Attributes
```

It is permissible to specify in the _including_ section, parameters
that have already been specified in the _included_ section and they
will take precedence. For instance, if `Monitor2` in the example
above should have a longer backend connect timeout it can be
specified as follows.
```
[Monitor2]
type=monitor
@include=Monitor-Common
servers=Server3, Server3
backend_connect_timeout = 5s
```

Note that an included section **must** be an `include` section and
that an `include` section **cannot** include another `include`
section. For instance, both of the following sections would cause
an error at startup.
```
[Monitor-Common]
type=include
@include=Base-Common
...

[Monitor2]
type=monitor
@include=Monitor1
...
```

Note also that if an included parameter is changed using `maxctrl`,
it will be changed _only_ on the actual object the change is applied
on, not on the `include` section where the parameter is originally
specified.

# Available Protocols

Protocol modules in MaxScale define what kind of clients can connect to a
listener and what type of backend servers are supported. Protocol is defined in
listener settings, and affects both the listener and any services the listener
is linked to.

## `MariaDB` or `MariaDBClient`

Implements MariaDB protocol. The listener will accept MariaDB/MySQL connections
from clients and route the client queries through a linked MaxScale service
to backend servers. The backends used by the service should be
MariaDB servers or compatible.

## `CDC`

See [Change Data Capture Protocol](../Protocols/CDC.md) for more information.

## `Postgresql` or `Postgresprotocol`

Implements [Postgresql protocol](https://www.postgresql.org/docs/current/protocol.html).
The listener will accept Postgresql connections from clients and route the
client queries through a linked MaxScale service to backend servers. The
backends used by the service should be PostgreSQL servers or compatible.

## `nosqlprotocol`

Accepts MongoDB® connections, yet stores and fetches results to/from
MariaDB servers. See [NoSQL documentation](../Protocols/NoSQL.md)
for more information.

# TLS/SSL encryption

This section describes configuration parameters for both servers and listeners
that control the TLS/SSL encryption method and the various certificate files
involved in it.

To enable TLS/SSL for a listener, you must set the `ssl` parameter to
`true` and provide at least the `ssl_cert` and `ssl_key` parameters.

To enable TLS/SSL for a server, you must set the `ssl` parameter to
`true`. If the backend database server has certificate verification
enabled, the `ssl_cert` and `ssl_key` parameters must also be defined.

Custom CA certificates can be defined with the `ssl_ca` parameter. If
`ssl_verify_peer_certificate` is enabled yet `ssl_ca` is not set, MaxScale
will load CA certificates from the system default location.

After this, MaxScale connections between the server and/or the client will be
encrypted. Note that the database must also be configured to use TLS/SSL
connections if backend connection encryption is used.

**Note:** MaxScale does not allow mixed use of TLS/SSL and normal connections on
  the same port.

If TLS encryption is enabled for a listener, any unencrypted connections to it
will be rejected. MaxScale does this to improve security by preventing
accidental creation of unencrypted connections.

The separation of secure and insecure connections differs from the MariaDB
Server which allows both secure and insecure connections on the same port. As
MaxScale is the gateway through which all connections go, MaxScale enforces
a stricter security policy than MariaDB Server. Multiple listeners with
different configurations can be created to enable different encryption schemes.

TLS encryption must be enabled for listeners when they are created. For servers,
the TLS can be enabled after creation but it cannot be disabled or altered.

Starting with MaxScale 2.5.20, if the TLS certificate given to MaxScale has the
X509v3 extended key usage information, MaxScale will check it and refuse to use
a certificate with the wrong usage. This means that a certificate with only
clientAuth can only be used with servers and a certificate with only serverAuth
can only be used with listeners. In order to use the same certificate for both
listeners and servers, it must have both the clientAuth and serverAuth usages.

### `ssl`

- **Type**: [boolean](#booleans)
- **Default**: false
- **Dynamic**: Yes

This enables SSL connections when set to true. The legacy values `required` and
`disabled` were removed in MaxScale 6.0.

If enabled, the certificate files mentioned above must also be
supplied. MaxScale connections to will then be encrypted with TLS/SSL.

### `ssl_key`

A string giving a file path that identifies an existing readable file. The file
must be the SSL client private key MaxScale should use. This is a required
parameter for listeners but an optional parameter for servers.

### `ssl_cert`

A string giving a file path that identifies an existing readable file. The file
must be the SSL client certificate MaxScale should use with the server. The
certificate must match the key defined in `ssl_key`. This is a required
parameter for listeners but an optional parameter for servers.

### `ssl_ca_cert`

Deprecated since MariaDB MaxScale 22.08. See `ssl_ca`.

### `ssl_ca`

A string giving a file path that identifies an existing readable file. The file
must be a Certificate Authority (CA) certificate. It will be used to verify
that the peer certificate (sent by either client or a MariaDB Server) is valid.
The CA certificate can consist of a certificate chain.

**NOTE** Up until MariaDB MaxScale 6, the parameter was called `ssl_ca_cert`,
         which is still accepted as an alias for `ssl_ca`.

### `ssl_version`

This parameter controls the minimum TLS version used. Accepted values are:

 * TLSv10
 * TLSv11
 * TLSv12
 * TLSv13 (not supported on OpenSSL 1.0)
 * MAX

E.g. setting `ssl_version=TLSv12` enables both TLSv12 and TLSv13. OpenSSL will
generally use the highest version supported by both ends.

The default setting (MAX) allows all supported versions. MaxScale supports
TLSv1.0, TLSv1.1, TLSv1.2 and TLSv1.3 depending on the OpenSSL library version.
TLSv1.0 and TLSv1.1 are considered deprecated and should not be used,
so setting `ssl_version=TLSv12` or `ssl_version=TLSv13` is recommended.

In MaxScale 23.08.3 and earlier, this setting defined the *only* allowed TLS
version, e.g. `ssl_version=TLSv12` would only enable TLSv12. The interpretation
changed in MaxScale 23.08.4 to allow the user to disable old versions while
enabling multiple recent TLS versions.

### `ssl_cipher`

Set the list of TLS ciphers. By default, no explicit ciphers are defined and the
system defaults are used. Note that this parameter does not modify TLSv1.3
ciphers.

### `ssl_cert_verify_depth`

The maximum length of the certificate authority chain that will be accepted. The
default value is 9, same as the OpenSSL default. The configured value must be
larger than 0.

### `ssl_verify_peer_certificate`

Peer certificate verification. This functionality is disabled by default. In
versions prior to 2.3.17 the feature was enabled by default.

When this feature is enabled, the peer (client or MariaDB Server) must send a
certificate. The certificate sent by the peer is verified against the
configured Certificate Authority to ensure the peer is who they claim to be.
For listeners, this behaves as if `REQUIRE X509` was defined for all users.

### `ssl_verify_peer_host`

- **Type**: [boolean](#booleans)
- **Default**: false
- **Dynamic**: Yes

Peer host verification.

When this feature is enabled, the peer (client or MariaDB Server) hostname or
IP is verified against the certificate sent by the peer. If the IP address or
the hostname does not match the one in the certificate, the connection is
closed.

If the peer does not provide a certificate, host verification is skipped.
To require peer certificates, also enable `ssl_verify_peer_certificate`.
For servers, the combination of
```
ssl_verify_peer_certificate=true
ssl_verify_peer_host=true
```
behaves like the `--ssl-verify-server-cert` command  line option for the
`mysql` client.

### `ssl_crl`

A string giving a file path that identifies an existing readable file. The file
must be a Certificate Revocation List in the PEM format that defines the revoked
certificates. This parameter is only accepted by listeners.

#### Example SSL enabled server configuration

```
[server1]
type=server
address=10.131.24.62
port=3306
ssl=true
ssl_cert=/usr/local/mariadb/maxscale/ssl/crt.max-client.pem
ssl_key=/usr/local/mariadb/maxscale/ssl/key.max-client.pem
ssl_ca_cert=/usr/local/mariadb/maxscale/ssl/crt.ca.maxscale.pem
```

This example configuration requires all connections to this server to be
encrypted with SSL. The paths to the certificate files and the Certificate
Authority file are also provided.

#### Example SSL enabled listener configuration

```
[RW-Split-Listener]
type=listener
service=RW-Split-Router
protocol=MariaDBClient
port=3306
ssl=true
ssl_cert=/usr/local/mariadb/maxscale/ssl/crt.maxscale.pem
ssl_key=/usr/local/mariadb/maxscale/ssl/key.csr.maxscale.pem
ssl_ca_cert=/usr/local/mariadb/maxscale/ssl/crt.ca.maxscale.pem
```

This example configuration requires all connections to be encrypted with
SSL. The paths to the certificate files and the Certificate Authority file are
also provided.

# Module Types

## Routing Modules

The main task of MariaDB MaxScale is to accept database connections from client
applications and route the connections or the statements sent over those
connections to the various services supported by MariaDB MaxScale.

Currently a number of routing modules are available, these are designed for a
range of different needs.

Connection based load balancing:
* [ReadConnRoute](../Routers/ReadConnRoute.md)

Read/Write aware statement based router:
* [ReadWriteSplit](../Routers/ReadWriteSplit.md)

Simple sharding on database level:
* [SchemaRouter](../Routers/SchemaRouter.md)

Binary log server:
* [Binlogrouter](../Routers/Binlogrouter.md)

## Monitor Modules

Monitor modules are used by MariaDB MaxScale to internally monitor the state of
the backend databases in order to set the server flags for each of those
servers. The router modules then use these flags to determine if the particular
server is a suitable destination for routing connections for particular query
classifications. The monitors are run within separate threads of MariaDB
MaxScale and do not affect MariaDB MaxScale's routing performance.

* [MariaDB Monitor](../Monitors/MariaDB-Monitor.md)
* [Galera Monitor](../Monitors/Galera-Monitor.md)

The use of monitors in MaxScale is not absolutely mandatory: it is possible to
run MariaDB MaxScale without a monitor module. In this case an external
monitoring system must the status of each server via MaxCtrl or the REST
API. **Only do this if you know what you are doing.**

## Filter Modules

![image alt text](images/image_10.png)

Filters provide a means to manipulate or process requests as they pass through
MariaDB MaxScale between the client side protocol and the query router. A full
explanation of each filter's functionality can be found in its documentation.

The [Filter Tutorial](../Tutorials/Filter-Tutorial.md) document shows how you
can add a filter to a service and combine multiple filters in one service.

* [Query Log All (QLA) Filter](../Filters/Query-Log-All-Filter.md)
* [Regular Expression Filter](../Filters/Regex-Filter.md)
* [Tee Filter](../Filters/Tee-Filter.md)
* [Top Filter](../Filters/Top-N-Filter.md)
* [Query Redirection Filter](../Filters/Named-Server-Filter.md)

# Encrypting Passwords

Passwords stored in the maxscale.cnf file may optionally be encrypted for added security.
This is done by creation of an encryption key on installation of MariaDB MaxScale.
Encryption keys may be created manually by executing the maxkeys utility with the argument
of the filename to store the key. The default location MariaDB MaxScale stores
the keys is `/var/lib/maxscale`. The passwords are encrypted using 256-bit AES CBC encryption.

```
 # Usage: maxkeys [PATH]
maxkeys /var/lib/maxscale/
```

Changing the encryption key for MariaDB MaxScale will invalidate any currently
encrypted keys stored in the maxscale.cnf file.

## Creating Encrypted Passwords

Encrypted passwords are created by executing the maxpasswd command with the location
of the .secrets file and the password you require to encrypt as an argument.

```
# Usage: maxpasswd PATH PASSWORD
maxpasswd /var/lib/maxscale/ MaxScalePw001
61DD955512C39A4A8BC4BB1E5F116705
```

The output of the maxpasswd command is a hexadecimal string, this should be inserted
into the maxscale.cnf file in place of the ordinary, plain text, password.
MariaDB MaxScale will determine this as an encrypted password and automatically decrypt
it before sending it the database server.

```
[Split-Service]
type=service
router=readwritesplit
servers=server1,server2,server3,server4
user=maxscale
password=61DD955512C39A4A8BC4BB1E5F116705
```

# Runtime Configuration Changes

Read the following documents for different methods of altering the MaxScale
configuration at runtime.

* MaxCtrl
  * [`create`](../Reference/MaxCtrl.md#create)
  * [`destroy`](../Reference/MaxCtrl.md#destroy)
  * [`add`](../Reference/MaxCtrl.md#add)
  * [`remove`](../Reference/MaxCtrl.md#remove)
  * [`alter`](../Reference/MaxCtrl.md#alter)

* [REST API](../REST-API/API.md) documentation

All changes to the configuration done via MaxCtrl are persisted as individual
configuration files in `/var/lib/maxscale/maxscale.cnf.d/`. The content of these
files will override any configurations found in the main configuration file or
any auxiliary configuration files.

Refer to the [Dynamic Configuration](#dynamic-configuration) section for more
details on how this mechanism works and how to disable it.

## Configuration Synchronization

The configuration synchronization mechanism is intended for synchronizing
configuration changes done on one MaxScale to all other MaxScales. This is done
by propagating the changes via the database cluster used by Maxscale.

When configuring configuration synchronization for the first time, the same
static configuration files should be used on all MaxScale instances that use the
same cluster: the value of `config_sync_cluster` must be the same on all
MaxScale instances and the cluster (i.e. the monitor) pointed by it and its
servers must be the same in every configuration.

Whenever the MaxScale configuration is modified at runtime, the latest
configuration is stored in the database cluster in the `mysql.maxscale_config`
table. The table is created when the first modification to the configuration is
done. A local copy of the configuration is stored in the data directory to allow
MaxScale to function even if a connection to the cluster cannot be made. By
default this file is stored at `/var/lib/maxscale/maxscale-config.json`.

Whenever MaxScale starts up, it checks if a local version of this configuration
exists. If it does and it is a valid cached configuration, the static
configuration file as well as any other generated configuration files are
ignored. The exception is the `[maxscale]` section of the main static
configuration file which is always read.

Each configuration has a version number with the initial configuration being
version 0. Each time the configuration is modified, the version number is
incremented. This version number is used to detect when MaxScale needs to update
its configuration.

### Error Handling in Configuration Synchronization

When doing a configuration change on the local MaxScale, if the configuration
change completes on MaxScale but fails to be committed to the database, MaxScale
will attempt to revert the local configuration change. If this attempt fails,
MaxScale will discard the cached configuration and abort the process.

When synchronizing with the cluster, if MaxScale fails to apply a configuration
retrieved from the cluster, it attempts to revert the configuration to the
previous version. If successful, the failed configuration update is ignored. If
the configuration update that fails cannot be reverted, the MaxScale
configuration will be in an indeterminate state. When this happens, MaxScale
will discard the cached configuration and abort the process.

When loading a locally cached configuration during startup, if any errors are
found in the cached configuration, it is discarded and the MaxScale process will
attempt to restart by exiting with code 75 from the main process. If MaxScale is
being used as a SystemD service, this will automatically trigger a restart of
MaxScale and no further actions are needed.

The most common reason for a failed configuration update is missing files. For
example, if a configuration update adds encrypted connections to a server and
the TLS certificates it uses were not copied over to all MaxScale nodes before
the change was done, the operation will fail on all nodes that do not have these
files.

If the synchronization of the configuration change fails at the step when the
database transaction is being committed, the new configuration can be
momentarily visible to the local MaxScale. This means the changes are not
guaranteed to be atomic on the local MaxScale but are atomic from the cluster's
point of view.

### Synchronization of Encrypted Passwords

Starting with MaxScale 6.4.9, any passwords that are transmitted by the
configuration synchronization are encrypted if password encryption has been
enabled in MaxScale. This means that all MaxScale nodes in the same
configuration cluster must be configured to use password encryption and they
need to all use the same encryption keys that were created with `maxkeys`.

### Managing Configuration Synchronization

The output of `maxctrl show maxscale` contains the `Config Sync` field with
information about the current configuration state of the local Maxscale as well
as the state of any other nodes using this cluster.

```
├──────────────┼─────────────────────────────────────────────────────────────┤
│ Config Sync  │ {                                                           │
│              │     "checksum": "3dd6b467760d1d2023f2bc3871a60dd903a3341e", │
│              │     "nodes": {                                              │
│              │         "maxscale": "OK",                                   │
│              │         "maxscale2": "OK"                                   │
│              │     },                                                      │
│              │     "origin": "maxscale",                                   │
│              │     "status": "OK",                                         │
│              │     "version": 2                                            │
│              │ }                                                           │
├──────────────┼─────────────────────────────────────────────────────────────┤
```

The `version` field is the logical configuration version and the `origin` is the
node that originates the latest configuration change. The `checksum` field is
the checksum of the logical configuration and can be used to compare whether two
Maxscale instances are in the same configuration state. The `nodes` field
contains the status of each MaxScale instance mapped to the hostname of the
server. This field is updated whenever MaxScale reads the configuration from the
cluster and can thus be used to detect which MaxScales have updated their
configuration.

The `mysql.maxscale_config` table where the configuration changes are stored
must not be modified manually. The only case when the table should be modified
is when resetting the configuration synchronization.

To reset the configuration synchronization:

1. Stop all MaxScale instances
2. Remove the cached configuration file stored at
   `/var/lib/maxscale/maxscale-config.json` on all MaxScale instances
3. Drop the `mysql.maxscale_config` table
4. Start all MaxScale instances

To disable configuration synchronization, remove `config_sync_cluster` from the
configuration file or set it to an empty string: `config_sync_cluster=""`. This
can be done at runtime with MaxCtrl by passing an empty string to
`config_sync_cluster`:

```
maxctrl alter maxscale config_sync_cluster ""
```

If MaxScale cannot create a connection to the database cluster, configuration
changes are not possible until communication with the database is possible. To
override this behavior and force the changes to be done, use the `--skip-sync`
option for maxctrl or the `sync=false` HTTP parameter for the REST API. Any
updates done with `--skip-sync` will overwritten by changes coming from the
cluster.

### Limitations in Configuration Synchronization

Only the MaxScale configuration is synchronized. Any external files (TLS
certificates, configuration files for modules or data generated by MaxScale) are
not synchronized. For example, the rule files for the cache filter must be
synchronized separately if the filter itself is modified.

Starting with MaxScale 22.08, the `Maintenance` and `Draining` states of servers
and modifications to the administrative users will be synchronized. In older
versions servers had to be put into maintenance mode and users had to be
modified separately on each MaxScale.

* ([MXS-3619](https://jira.mariadb.org/browse/MXS-3619)) External files are not
  synchronized.

* ([MXS-4276](https://jira.mariadb.org/browse/MXS-4276)) The `--export-config`
  option will not export the cluster configuration and instead exports only the
  static configuration files. To start a new MaxScale based off of a clustered
  configuration, copy the static configuration files as well as the JSON
  configuration in `/var/lib/maxscale/maxscale-config.json` to the new MaxScale
  instance.

## Backing Up Configuration Changes

The combination of configuration files can be done either manually
(e.g. `rsync`) or with the `maxscale --export-config=FILE` command line
option. See `maxscale --help` for more information about how to use the
`--export-config` flag.

For example, to export the current runtime configuration, run the following
command.

```
maxscale --export-config=/tmp/maxscale.cnf.combined
```

This will create the `/tmp/maxscale.cnf.combined` file and write the current
configuration into the it. This allows new MaxScale instances to be easily set
up without requiring copying of all runtime configuration files. The user
executing the command must be able to read all MaxScale configuration files as
well as create and write the provided filename.

# Encryption Key Managers

The encryption key managers are how MaxScale retrieves symmetric encryption keys
from a key management system. Some parts of MaxScale require the `key_manager`
to be configured in order to work. The key manager that is used is selected with
the [`key_manager`](#key_manager) parameter and the key manager itself is
configured by placing the parameters in the `[maxscale]` section.

The encryption key managers can be enabled at runtime using `maxctrl alter
maxscale` but cannot be disabled once enabled. To disable the encryption key
management, stop Maxscale, remove any persisted configuration files and remove
`key_manager` as well as any key manager options from the static configuration
files.

## File-based Key Manager

The encryption keys are stored in a text file stored on a local filesystem.

The file uses the same format as the MariaDB server [File Key Management
Encryption Plugin]
(https://mariadb.com/kb/en/file-key-management-encryption-plugin/): a file
consisting of an encryption key ID number and the hex-encoded encryption key
separated by a semicolon. Read [Creating the Key
File](https://mariadb.com/kb/en/file-key-management-encryption-plugin/#creating-the-key-file)
for more details on how to create the file.

For example, to configure encryption for the `nosqlprotocol` shared credentials
using the file-based encryption key:

1. Create the key file with `(echo -n '1;' ; openssl rand -hex 32) | cat > /var/lib/maxscale/encryption.key`

2. Give MaxScale read permissions on it with `chown maxscale:maxscale /var/lib/maxscale/encryption.key`

3. Configure MaxScale with the following:

```
[maxscale]
key_manager=file
file.keyfile=/var/lib/maxscale/encryption.key

[NoSQL-Listener]
type=listener
service=My-Service
protocol=nosqlprotocol
nosqlprotocol.authentication_key_id=1
nosqlprotocol.authentication_user=my_user
nosqlprotocol.authentication_password=my_password

# Add services, servers, monitors etc.
```

4. Start MaxScale

### Limitations

* Key versioning is not supported

### Parameters

#### `file.keyfile`

- **Type**: path
- **Mandatory**: Yes
- **Dynamic**: Yes

Path to the file that contains the encryption keys. The user MaxScale runs as
(almost always `maxscale`) must be able to read this file. Encryption keys are
read from disk only during startup or when any global MaxScale parameter is
modified at runtime.

## KMIP Key Manager

Encryption keys are read from a KMIP server.

The KMIP key manager has been verified to work with the PyKMIP server.

### Limitations

* Key versioning is not supported

* Encryption keys are not cached locally: whenever MaxScale needs an encryption
  key, it retrieves it from the KMIP server.

### Parameters

#### `kmip.host`

- **Type**: string
- **Mandatory**: Yes
- **Dynamic**: Yes

The host where the KMIP server is.

#### `kmip.port`

- **Type**: integer
- **Mandatory**: Yes
- **Dynamic**: Yes

The port on which the KMIP server listens on.

#### `kmip.cert`

- **Type**: path
- **Mandatory**: Yes
- **Dynamic**: Yes

The client public certificate used when connecting to the KMIP server.

#### `kmip.key`

- **Type**: path
- **Mandatory**: Yes
- **Dynamic**: Yes

The client private key used when connecting to the KMIP server.

#### `kmip.ca`

- **Type**: path
- **Default**: `""`
- **Dynamic**: Yes

The CA certificate to use. By default the system default certificates are used.

## HashiCorp Vault Key Manager

Encryption keys are read from a local or remote Vault server using the secret
engine included in the Vault. This key manager supports versioned keys. Only
version 2 key-value stores are supported.

The encryption keys use the same format as the MariaDB [HashiCorp Vault Key
Management Plugin](https://mariadb.com/kb/en/hashicorp-key-management-plugin/):
The key-value secret for each encryption key ID must contain the field `data`
which must contain a hex-encoded string that is either 32, 48 or 64 characters
long.

An easy way to generate a correct encryption key is to use the `vault` and
`openssl` command line clients. The following command creates a 256-bit
encryption key using `openssl` and stores it using the key ID `1`:

```
$ openssl rand -hex 32|vault kv put secret/1 data=-
== Secret Path ==
secret/data/1

======= Metadata =======
Key                Value
---                -----
created_time       2022-06-23T06:50:55.29063873Z
custom_metadata    <nil>
deletion_time      n/a
destroyed          false
version            1
```

### Limitations

* Encryption keys are not cached locally: whenever MaxScale needs an encryption
  key, it retrieves it from the Vault server.

### Parameters

#### `vault.token`

- **Type**: password
- **Mandatory**: Yes
- **Dynamic**: Yes

The authentication token used to connect to the Vault server. This can be
encrypted using `maxpasswd`, similar to how other passwords are encrypted.

#### `vault.host`

- **Type**: string
- **Default**: `localhost`
- **Dynamic**: Yes

The host where the Vault server is.

#### `vault.port`

- **Type**: integer
- **Default**: `8200`
- **Dynamic**: Yes

The port on which the Vault server listens on.

#### `vault.ca`

- **Type**: path
- **Default**: `""`
- **Dynamic**: Yes

The CA certificate to use. By default the system default certificates are used.

#### `vault.tls`

- **Type**: [boolean](#booleans)
- **Default**: true
- **Dynamic**: Yes

Whether to use encrypted connections (i.e. HTTPS or HTTP) when communicating
with the Vault server.

#### `vault.mount`

- **Type**: string
- **Default**: `secret`
- **Dynamic**: Yes

The Key-Value mount where the secret is stored. By default the `secret` mount is
used which is present by default in most Vault installations.

#### `vault.timeout`

- **Type**: [duration](#durations)
- **Default**: 30s
- **Dynamic**: Yes

The connection and request timeout used with the Vault server.

# Threads

For routing, MaxScale uses asynchronous I/O and a fixed number of threads
(aka _routing workers_), whose number up until 23.02 was fixed at startup.
From 23.02 onwards the number of threads can be altered at runtime, which
is convenient, for instance, if MaxScale is running in a container whose
properties are changed during the lifetime of the container.

A thread can be in three different states:
   * **Active**: The thread is routing client traffic and is listening
     for new connections.
   * **Draining**: The thread is routing client traffic but is **not**
     listening for new connections.
   * **Dormant**: The thread is not routing client traffic (all sessions
     have ended), and is not listening for new connections, and is waiting
     to be terminated.

All threads start as _Active_ and may become _Draining_ if the number of
threads is reduced. A draining thread will eventually become _Dormant_,
unless the number of threads is increased while the thread is still _Draining_.

Note that it is not possible to terminate a specific thread, but it is only
possible to specify the _number_ of threads that MaxScale should use, and
that the threads will be terminated from the end. This has implications
if the number of threads is reduced by more than 1, as a _Dormant_ thread
will not be terminated before it is the last thread.

In the following, MaxScale has been started with `threads=4`.
```
$ bin/maxctrl show threads
┌────────────────────────┬────────┬────────┬────────┬────────┬─────┐
│ Id                     │ 0      │ 1      │ 2      │ 3      │ All │
├────────────────────────┼────────┼────────┼────────┼────────┼─────┤
│ State                  │ Active │ Active │ Active │ Active │ N/A │
├────────────────────────┼────────┼────────┼────────┼────────┼─────┤
...
```
All threads are _Active_. If we now decrease the number of threads
```
$ bin/maxctrl alter maxscale threads=2
OK
$ bin/maxctrl show threads
┌────────────────────────┬────────┬────────┬──────────┬──────────┬─────────┐
│ Id                     │ 0      │ 1      │ 2        │ 3        │ All     │
├────────────────────────┼────────┼────────┼──────────┼──────────┼─────────┤
│ State                  │ Active │ Active │ Draining │ Draining │ N/A     │
├────────────────────────┼────────┼────────┼──────────┼──────────┼─────────┤
...
```
we will see that the threads 2 and 3 are now _Draining_. The reason is
that threads 2 and 3 still handle client sessions. If some client sessions
now end, the situation may become like
```
┌────────────────────────┬────────┬────────┬─────────┬──────────┬────────┐
│ Id                     │ 0      │ 1      │ 2       │ 3        │ All    │
├────────────────────────┼────────┼────────┼─────────┼──────────┼────────┤
│ State                  │ Active │ Active │ Dormant │ Draining │ N/A    │
├────────────────────────┼────────┼────────┼─────────┼──────────┼────────┤
...
```
That is, thread 2 is _Dormant_ and thread 3 is _Draining_. All client sessions
that were handled by thread 2 have ended and the thread is ready to be
terminated. However, as thread 3 is still _Draining_, thread 2 will not be
terminated but stay _Dormant_.

If the sessions handled by thread 3 end, then it will become _Dormant_ at
which point first thread 3 will be terminatad and immediately after that
thread 2.
```
$ bin/maxctrl show threads
┌────────────────────────┬────────┬────────┬──────┐
│ Id                     │ 0      │ 1      │ All  │
├────────────────────────┼────────┼────────┼──────┤
│ State                  │ Active │ Active │ N/A  │
├────────────────────────┼────────┼────────┼──────┤
...
```
If the situation is like
```
$ bin/maxctrl show threads
┌────────────────────────┬────────┬────────┬─────────┬──────────┬────────┐
│ Id                     │ 0      │ 1      │ 2       │ 3        │ All    │
├────────────────────────┼────────┼────────┼─────────┼──────────┼────────┤
│ State                  │ Active │ Active │ Dormant │ Draining │ N/A    │
├────────────────────────┼────────┼────────┼─────────┼──────────┼────────┤
...
```
that is, the number of threads was 4 but has been reduced to 2, and while
thread 2 has become drained it stays as _Dormant_ since thread 3 is still
_Draining_, it is possible to make thread 2 _Active_ again by increasing the
number of threads to 3.
```
$ bin/maxctrl alter maxscale threads=3
OK
wikman@johan-P53s:maxscale $ bin/maxctrl show threads
┌────────────────────────┬────────┬────────┬────────┬──────────┬────────┐
│ Id                     │ 0      │ 1      │ 2      │ 3        │ All    │
├────────────────────────┼────────┼────────┼────────┼──────────┼────────┤
│ State                  │ Active │ Active │ Active │ Draining │ N/A    │
├────────────────────────┼────────┼────────┼────────┼──────────┼────────┤
...
```
Once the sessions of thread 3 ends, we will have
```
$ bin/maxctrl show threads
┌────────────────────────┬────────┬────────┬────────┬──────┐
│ Id                     │ 0      │ 1      │ 2      │ All  │
├────────────────────────┼────────┼────────┼────────┼──────┤
│ State                  │ Active │ Active │ Active │ N/A  │
├────────────────────────┼────────┼────────┼────────┼──────┤
...
```
# Error Reporting

MariaDB MaxScale is designed to be executed as a service, therefore all error
reports, including configuration errors, are written to the MariaDB MaxScale
error log file. By default, MariaDB MaxScale will log to a file in
`/var/log/maxscale` and the system log.

# Limitations

The current limitations of MaxScale are listed in the [Limitations](../About/Limitations.md) document.

# Performance Optimization

* Tune `query_classifier_cache_size` to allow maximal use of the query
  classifier cache. Increase the value and/or system memory until the set of
  unique SQL patterns fits into memory. By default at most 15% of the system
  memory is used for this cache. To detect if the SQL statements fit into
  memory, monitor the `QC cache evictions` value in `maxctrl show threads` to
  see how many evictions take place. If it keeps increasing, increase the size
  of the query classifier cache. Using the query classifier cache with a CPU
  bound workload gives a roughly 20% improvement in performance compared to when
  it is turned off.

* A faster CPU with more CPU cores is better. This is true for most applications
  but especially for MaxScale as it is mostly limited by the speed of the
  CPU. Using `threads=auto` is recommended (the default starting with MaxScale
  6).

* Network throughput between the client, MaxScale and the database nodes governs
  how much traffic can be handled. The client-to-MaxScale network is likely to
  be saturated first: having multiple MaxScales in front of the cluster is an
  easy way of solving this problem.

* Certain MaxScale modules store data on disk. A faster disk improves their
  performance but depending on the module, this might not be a big enough of a
  problem to worry about. Filters like the `qlafilter` that write information to
  disk for every SQL query can cause performance bottlenecks.

## MaxScale Diagnostics using MaxCtrl

From 22.08.2 onwards, `maxctrl show maxscale` shows a `System` object with
information about the system MaxScale is running on. The fields are:

| Field | Meaning |
|-------|---------|
| `machine.cores_physical` | The number of physical CPU cores on the machine. |
| `machine.cores_available` | The number of CPU cores available to MaxScale. This number may be smaller than `machine.cores_physical`, if CPU affinities are used and only a subset of the physical cores are available to MaxScale. |
| `machine.cores_virtual` | The number of virtual CPU cores available to MaxScale. This number may be a decimal and smaller than `machine.cores_available`, if MaxScale is running in a container whose CPU quota and period has been restricted. Note that if MaxScale is not, or fails to detect it is running in a container, the value shown will be identical with `machine.cores_available`. |
| `machine.memory_physical` | The amount of physical memory on the machine.|
| `machine.memory_available` | The amount of memory available to MaxScale. This number may be smaller than `machine.memory_physical`, if MaxScale is running in a container whose memory has been restricted. Note that if MaxScale is not, or fails to detect it is running in a container, the value shown will be identical with `machine.memory_physical`. Note also that the amount is available to all processes running in the same container, not just to MaxScale.|
| `maxscale.query_classifier_cache_size` | The _maximum_ size of the MaxScale query classifier cache.|
| `maxscale.threads` | The number of routing threads used by MaxScale.|

In addition there is an `os` object that contains what the Linux command `uname` displays.

### Configuration

#### `threads`

If `threads` has not been specified at all in the MaxScale configuration file,
or if its value is `auto`, then MaxScale will use as many routing threads as
there are physical cores on the machine. This is the right choice, if MaxScale
is running on a dedicated machine or in a container that has not been restricted
in any way.

However, if the number of cores available to MaxScale have been restricted or
if MaxScale is running in a container whose CPU quota and period have been
limited, then it will lead to MaxScale using more routing threads than what
is appropriate in the environment where it is running.

If `machine.cores_virtual` is less than `machine.cores_physical`, then `threads`
should be specified explicitly in the MaxScale configuration file and its value
should be that of `machine.cores_virtual` rounded up to the nearest integer. If
that value is `1` it may be beneficial to check whether `2` gives better performance.

#### `query_classifier_cache_size`

If `query_classifier_cache_size` has not been specified in the MaxScale
configuration file, then MaxScale will use at most 15% of the amount of physical
memory in the machine for the cache. This is a good starting point, if MaxScale
is running on a dedicated machine or in a container that has not been restricted
in any way. Note that the amount specifies how much memory the cache at maximum
is allowed to use, not what would immediately be allocated for the cache.

However, if the amount of memory available to MaxScale has been restricted,
which may be the case if MaxScale is running in a container, this may cause the
cache to grow beyond what is available, which will lead to a crash or MaxScale
being killed.

If the value of `machine.memory_available` is less than that of
`machine.memory_physical`, then `query_classifier_cache_size` should be explicitly
set to 15% of `maxscale.memory_available`. The value can be larger, but must not
be a bigger share of `machine.memory_available` than what is reasonable.

### Example

```
$ maxctrl show maxscale
...
├──────────────┼────────────────────────────────────────────────────────────────────────────┤
│ System       │ {                                                                          │
│              │     "machine": {                                                           │
│              │         "cores_available": 8,                                              │
│              │         "cores_physical": 8,                                               │
│              │         "cores_virtual": 4,                                                │
│              │         "memory_available": 20858544128,                                   │
│              │         "memory_physical": 41717088256                                     │
│              │     },                                                                     │
│              │     "maxscale": {                                                          │
│              │         "query_classifier_cache_size": 6257563238,                         │
│              │         "threads": 8                                                       │
│              │     },                                                                     │
│              │     "os": {                                                                │
│              │         "machine": "x86_64",                                               │
│              │         "nodename": "johan-P53s",                                          │
│              │         "release": "5.4.0-125-generic",                                    │
│              │         "sysname": "Linux",                                                │
│              │         "version": "#141~18.04.1-Ubuntu SMP Thu Aug 11 20:15:56 UTC 2022"  │
│              │     }                                                                      │
│              │ }                                                                          │
└──────────────┴────────────────────────────────────────────────────────────────────────────┘
```
As can be seen, `maxscale.threads` is larger than `machine.cores_virtual` and thus,
`threads=4` should explicitly be specified in the MaxScale configuration file.

`maxscale.query_classifier_cache_size` is the default 15% of `machine.memory_physical`
but as `machine.memory_available` is just half of that, something like
`query_classifier_cache_size=3100000000` (~15% of `machine.memory_available`) should be
added to the configuration file.

```
[maxscale]
threads=4
query_classifier_cache_size=3100000000
...
```

# Troubleshooting

For a list of common problems and their solutions, read the
[MaxScale Troubleshooting](https://mariadb.com/kb/en/maxscale-troubleshooting/)
article on the MariaDB Knowledge Base.

## Systemd Watchdog

If MaxScale is running as a systemd service, the systemd Watchdog will be
enabled by default. To configure it, change the `WatchdogSec` option in the
Service section of the maxscale systemd configuration file located in
`/lib/systemd/system/maxscale.service`:

```
WatchdogSec=30s
```

It is not recommended to use a watchdog timeout less than 30 seconds. When
enabled MaxScale will check that all threads are running and notify systemd
with a "keep-alive ping".

Systemd reference: https://www.freedesktop.org/software/systemd/man/systemd.service.html
