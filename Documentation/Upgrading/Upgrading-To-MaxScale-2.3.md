# Upgrading MariaDB MaxScale from 2.2 to 2.3

This document describes possible issues when upgrading MariaDB
MaxScale from version 2.2 to 2.3.

For more information about MariaDB MaxScale 2.3, please refer
to the [ChangeLog](../Changelog.md).

Before starting the upgrade, we recommend you back up your current
configuration file.

## Increased Memory Use

Starting with MaxScale 2.3.0 up to 40% of the memory can be used for
caching parsed queries. The most noticeable change is that it improves
performance in almost all cases where queries need to be parsed. Most of
the time this happens when the readwritesplit router or filters are used.

The amount of memory that MaxScale uses can be controlled with the
`query_classifier_cache_size` parameter. For example, to limit the total
memory to 1GB, add `query_classifier_cache_size=1G` to your
configuration. To disable it, set the value to `0`.

In addition to the aforementioned query classifier caching, the
readwritesplit session command history is enabled by default in 2.3 but is
limited to a maximum of 50 commands after which the history is
disabled. This is unlikely to show in any metrics but it contributes to
the increased memory foorprint of MaxScale.

## Unknown Global Parameters

All unknown parameters are now treated as errors. Check your configuration for
errors if MaxScale fails to start after upgrading to 2.3.1.

## `passwd` is deprecated

In the configuration file, passwords for monitors and services should be
specified using `password`; the support for the deprecated
`passwd` will be removed in the future. That is, the following
```
[The-Service]
type=service
passwd=some-service-password
...

[The-Monitor]
type=monitor
passwd=some-monitor-password
...
```
should be changed to
```
[The-Service]
type=service
password=some-service-password
...

[The-Monitor]
type=monitor
password=some-monitor-password
...
```
