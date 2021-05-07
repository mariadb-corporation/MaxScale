# Upgrading MariaDB MaxScale from 2.5 to 2.6

This document describes possible issues when upgrading MariaDB MaxScale from
version 2.5 to 2.6.

For more information about MaxScale 2.6, refer to the
[ChangeLog](../Changelog.md).

Before starting the upgrade, any existing configuration files should be backed
up.

## Duration Type Parameters

Using duration type parameters without an explicit suffix has been deprecated in
MaxScale 2.4. In MaxScale 2.6 they are no longer allowed when used with the REST
API or MaxCtrl. This means that any `create` or `alter` commands in MaxCtrl that
use a duration type parameter must explicitly specify the suffix of the unit.

For example, the following command:

```
maxctrl alter service My-Service connection_keepalive 30000
```

should be replaced with:

```
maxctrl alter service My-Service connection_keepalive 30000ms
```

Duration type parameters can still be defined in the configuration file without
an explicit suffix but this behavior is deprecated. The recommended approach is
to add explicit suffixes to all duration type parameters when upgrading to
MaxScale 2.6.

## Changed Parameters

### `threads`

The default value of `threads` was changed to `auto`.

## Removed Parameters

### Core Parameters

The following deprecated core parameters have been removed:

- `thread_stack_size`

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
