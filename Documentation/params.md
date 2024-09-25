[storage](Filters/Cache.md#storage)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `storage_inmemory`

[storage_options](Filters/Cache.md#storage_options)
- **Type**: string
- **Mandatory**: No
- **Dynamic**: No
- **Default**:

[hard_ttl](Filters/Cache.md#hard_ttl)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `0s` (no limit)

[soft_ttl](Filters/Cache.md#soft_ttl)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `0s` (no limit)

[max_resultset_rows](Filters/Cache.md#max_resultset_rows)
- **Type**: count
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `0` (no limit)

[max_resultset_size](Filters/Cache.md#max_resultset_size)
- **Type**: [size](../Getting-Started/Configuration-Guide.md#sizes)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `0` (no limit)

[max_count](Filters/Cache.md#max_count)
- **Type**: count
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `0` (no limit)

[max_size](Filters/Cache.md#max_size)
- **Type**: [size](../Getting-Started/Configuration-Guide.md#sizes)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `0` (no limit)

[rules](Filters/Cache.md#rules)
- **Type**: path
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `""` (no rules)

[cached_data](Filters/Cache.md#cached_data)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `shared`, `thread_specific`
- **Default**: `thread_specific`

[selects](Filters/Cache.md#selects)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `assume_cacheable`, `verify_cacheable`
- **Default**: `assume_cacheable`

[cache_in_transactions](Filters/Cache.md#cache_in_transactions)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `never`, `read_only_transactions`, `all_transactions`
- **Default**: `all_transactions`

[debug](Filters/Cache.md#debug)
- **Type**: number
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `0`

[enabled](Filters/Cache.md#enabled)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `true`

[invalidate](Filters/Cache.md#invalidate)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `never`, `current`
- **Default**: `never`

[clear_cache_on_parse_errors](Filters/Cache.md#clear_cache_on_parse_errors)
- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `true`

[users](Filters/Cache.md#users)
- **Type**: [enum](../Getting-Started/Configuration-Guide.md#enumerations)
- **Mandatory**: No
- **Dynamic**: No
- **Values**: `mixed`, `isolated`
- **Default**: `mixed`

[timeout](Filters/Cache.md#timeout)
- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Mandatory**: No
- **Dynamic**: No
- **Default**: `5s`

