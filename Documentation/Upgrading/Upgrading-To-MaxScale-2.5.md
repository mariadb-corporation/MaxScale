# Upgrading MariaDB MaxScale from 2.4 to 2.5

This document describes possible issues when upgrading MariaDB
MaxScale from version 2.4 to 2.5.

For more information about MaxScale 2.5, refer to the
[ChangeLog](../Changelog.md).

Before starting the upgrade, any existing configuration files should be
backed up.

## MaxAdmin

The deprecated MaxAdmin interface has been removed in 2.5.0 in favor of the REST
API and the MaxCtrl command line client. The `cli` and `maxscaled` modules can
no longer be used.

## Authentication

The credentials used by services now require additional grants. For a full list
of required grants, refer to the
[protocol documentation](../Authenticators/Authentication-Modules.md#required-grants).

## MariaDB-Monitor

The settings `detect_stale_master`, `detect_standalone_master` and
`detect_stale_slave`  are replaced by `master_conditions` and
`slave_conditions`. The old settings may still be used, but will be removed in
a later version.

### Password encryption

The encrypted passwords feature has been updated to be more secure. Users are
recommended to generate a new encryption key and and re-encrypt their passwords
using the `maxkeys` and `maxpasswd` utilities. Old passwords still work.

## Columnstore Monitor

It is now mandatory to specify in the configuration what version the
monitored Columnstore cluster is.
```
[CSMonitor]
type=monitor
module=csmon
version=1.5
...
```
Please see the [documentation](../Monitors/ColumnStore-Monitor.md#master-selection)
for details.

## New binlog router

The binlog router delivered with MaxScale 2.5 is completely new and
not 100% backward compatible with the binlog router delivered with
earlier MaxScale versions. If you use the binlog router, carefully
assess whether the functionality provided by the new one fulfills
your requirements, before upgrading MaxScale.
