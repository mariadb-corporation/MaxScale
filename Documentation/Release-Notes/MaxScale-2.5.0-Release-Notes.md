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
* `auth_read_timeout` changed from 1 to 10 seconds
* `auth_write_timeout` changed from 2 to 10 seconds

## Dropped Features

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

* Listeners support multiple authenticators.

### Cache Invalidation

The MaxScale cache is now capable of performing invalidation of cache
entries. See the
[documentation](../Filters/Cache.md#invalidation)
for more information.

### New Cache Storage Modules

The following storage modules have been added:
* [storage_memcached](../Filters/Cache.md#storage_memcached)

### Load Rebalancing

If the load of the threads of MaxScale differs by a certain amount,
MaxScale is now capable of moving sessions from one thread to another
so that all threads are evenly utilized. Please refer to
[rebalance_period](../Getting-Started/Configuration-Guide.md#rebalance_period)
and
[rebalance_threshold](../Getting-Started/Configuration-Guide.md#rebalance_threshold)
for more information.

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
