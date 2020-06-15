# MariaDB MaxScale 2.5.0 Release Notes

Release 2.5.0 is a Beta release.

This document describes the changes in release 2.5.0, when compared to
release 2.4.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

### `connection_keepalive`

Previously this feature was a readwritesplit feature. In MaxScale 2.5.0 it has
been converted into a core MaxScale feature and it can be used with all
routers. In addition to this, the keepalive mechanism now also keeps completely
idle connections alive (MXS-2505).

### User loading timeouts

The default timeout values for user loading have been changed.

* `auth_connect_timeout` changed from 3 to 10 second
* `auth_read_timeout` deprecated and ignored
* `auth_write_timeout` deprecated and ignored

### Setting a server to maintenance or draining mode

In the case of a regular MariaDB cluster (monitored by the MariaDB monitor),
it is no longer possible to set the the master server to maintenance or
draining mode, but a switchover must be performed first.

### MariaDB-Monitor deprecated settings

The settings `detect_stale_master`, `detect_standalone_master` and
`detect_stale_slave`  are replaced by the more flexible `master_conditions` and
`slave_conditions`. The old settings may still be used, but will be removed in
a later version.

### Password encryption

The encrypted passwords feature has been updated to be more secure. Users are
recommended to generate a new encryption key and and re-encrypt their passwords
using the `maxkeys` and `maxpasswd` utilities. Old passwords still work.

### Authenticator options

Several changes, see [here](../Authenticators/Authentication-Modules.md)
for more details.

## Dropped Features

### MaxAdmin has been removed.

Use maxctrl or maxgui instead.

### No concept of Unix users anymore

As maxadmin, that could be used over a unix domain socket, has been
removed it is no longer possible to enable/disable unix users as
MaxScale administrators.

### Configuration parameters

The following deprecated parameters have been removed.

* `non_blocking_polls`
* `poll_sleep`
* `log_trace` and `log_messages` that were synonyms for `log_info` and
  `log_notice` respectively.

### Removed modules

* The **maxinfo**-router and the **httpd**-protocol have been removed.

## Deprecated Features

* Server parameters `protocol` and `authenticator` have been deprecated. Any
  definitions are ignored.

### Readwritesplit

* The use of percentage values in `max_slave_connections` has been deprecated.

## New Features

* The timeout to maxctrl can now be specified using duration suffixes, e.g.
  `--timeout 5s`.

### MaxGUI

MaxGUI is a new browser based configuration and management tool for
MaxScale that complements the command line tool `maxctrl`.

Please see the
[documentation](../Getting-Started/MaxGUI.md)
for more information.

### Cache

#### Invalidation

The MaxScale cache is now capable of performing invalidation of cache
entries. See the
[documentation](../Filters/Cache.md#invalidation)
for more information.

#### New Cache Storage Modules

The following storage modules have been added:
* [storage_memcached](../Filters/Cache.md#storage_memcached)
* [storage_redis](../Filters/Cache.md#storage_redis)

When either of these are used, the cache can be shared between
two MaxScale instances.

#### User specific cache

It is now possible to specify that each user should have a
cache of his/her own. Having a user-specific cache ensures that
it is impossible for a user to obtain access to data he/she should
not have access to, something which may be possible if a shared
cache is used, as the cache filter is not aware of grants.
Please see the
[documentation](../Filters/Cache.md#users)
for details.

### Load Rebalancing

If the load of the threads of MaxScale differs by a certain amount,
MaxScale is now capable of moving sessions from one thread to another
so that all threads are evenly utilized. Please refer to
[rebalance_period](../Getting-Started/Configuration-Guide.md#rebalance_period)
and
[rebalance_threshold](../Getting-Started/Configuration-Guide.md#rebalance_threshold)
for more information.

### Columnstore support

The Columnstore monitor is now capable of monitoring Columnstore
versions 1.0, 1.2 and 1.5. In addition, some cluster management
operations are available for Columnstore 1.5. Please see the
[documentation](../Monitors/ColumnStore-Monitor.md)
for details.

### Cooperative monitoring

MariaDB-Monitor supports cooperative monitoring. See
[cooperative monitoring](../Monitors/MariaDB-Monitor.md#cooperative-monitoring)
for more information.

### Listeners and authenticators

* multiple authenticators
* connection_init_sql_file
* mysql_clear_password support for PAM authenticator

See [configuration guide](../Getting-Started/Configuration-Guide.md#authenticator)
and [pam plugin documentation](../Authenticators/PAM-Authenticator.md)
for more details.

## Bug fixes

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the Linux distributions supported
by MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/resources/downloads).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is X.Y.Z. Further, *master* always refers to the latest released non-beta version.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
