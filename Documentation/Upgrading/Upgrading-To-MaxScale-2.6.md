# Upgrading MariaDB MaxScale from 2.5 to 2.6

This document describes possible issues when upgrading MariaDB MaxScale from
version 2.5 to 2.6.

For more information about MaxScale 2.6, refer to the
[ChangeLog](../Changelog.md).

Before starting the upgrade, any existing configuration files should be backed
up.

## Removed Parameters

### Schemarouter

The deprecated aliases for the schemarouter parameters `ignore_databases` and
`ignore_databases_regex` have been removed. They can be replaced with
`ignore_tables` and `ignore_tables_regex`.

In addition, the `preferred_server` parameter that was deprecated in 2.5 has
also been removed.

## Session Command History

The `prune_sescmd_history`, `max_sescmd_history` and `disable_sescmd_history`
have been made into generic service parameters that are shared between all
routers that support it.

The default value of `prune_sescmd_history` was changed from `false` to
`true`. This was done as most MaxScale installations either benefit from it
being enabled or are not affected by it.
