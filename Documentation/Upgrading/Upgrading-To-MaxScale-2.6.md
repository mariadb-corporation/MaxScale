# Upgrading MariaDB MaxScale from 2.5 to 2.6

This document describes possible issues when upgrading MariaDB MaxScale from
version 2.5 to 2.6.

For more information about MaxScale 2.6, refer to the
[ChangeLog](../Changelog.md).

Before starting the upgrade, any existing configuration files should be backed
up.

## Session Command History

The `prune_sescmd_history`, `max_sescmd_history` and `disable_sescmd_history`
have been made into generic service parameters that are shared between all
routers that support it.

The default value of `prune_sescmd_history` was changed from `false` to
`true`. This was done as most MaxScale installations either benefit from it
being enabled or are not affected by it.
