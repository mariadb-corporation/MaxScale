# Upgrading MariaDB MaxScale from 2.4 to 2.5

This document describes possible issues when upgrading MariaDB
MaxScale from version 2.4 to 2.5.

For more information about MaxScale 2.5, refer to the
[ChangeLog](../Changelog.md).

Before starting the upgrade, any existing configuration files should be
backed up.

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
version=1.2
...
```
Please see the [documentation](../Monitors/ColumnStore-Monitor.md#master-selection)
for details.
