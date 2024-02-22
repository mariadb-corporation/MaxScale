# Masking

This filter was introduced in MariaDB MaxScale 2.1.

[TOC]

## Overview

With the _masking_ filter it is possible to obfuscate the returned
value of a particular column.

For instance, suppose there is a table _person_ that, among other
columns, contains the column _ssn_ where the social security number
of a person is stored.

With the masking filter it is possible to specify that when the _ssn_
field is queried, a masked value is returned unless the user making
the query is a specific one. That is, when making the query
```
> SELECT name, ssn FROM person;
```
instead of getting the real result, as in
```
+-------+-------------+
+ name  | ssn         |
+-------+-------------+
| Alice | 721-07-4426 |
| Bob   | 435-22-3267 |
...
```
the _ssn_ would be masked, as in
```
+-------+-------------+
+ name  | ssn         |
+-------+-------------+
| Alice | XXX-XX-XXXX |
| Bob   | XXX-XX-XXXX |
...
```

Note that the masking filter should be viewed as a best-effort solution
intended for protecting against accidental misuse rather than malicious
attacks.

## Security

From MaxScale 2.3 onwards, the masking filter will reject statements
that use functions in conjunction with columns that should be masked.
Allowing function usage provides a way for circumventing the masking,
unless a firewall filter is separately configured and installed.

Please see the configuration parameter
[prevent_function_usage](#prevent_function_usage)
for how to change the default behaviour.

From MaxScale 2.3.5 onwards, the masking filter will check the
definition of user variables and reject statements that define a user
variable using a statement that refers to columns that should be masked.

Please see the configuration parameter
[check_user_variables](#check_user_variables)
for how to change the default behaviour.

From MaxScale 2.3.5 onwards, the masking filter will examine unions
and if the second or subsequent SELECT refer to columns that should
be masked, the statement will be rejected.

Please see the configuration parameter
[check_unions](#check_unions)
for how to change the default behaviour.

From MaxScale 2.3.5 onwards, the masking filter will examine subqueries
and if a subquery refers to columns that should be masked, the statement
will be rejected.

Please see the configuration parameter
[check_subqueries](#check_subqueries)
for how to change the default behaviour.

Note that in order to ensure that it is not possible to get access to
masked data, the privileges of the users should be minimized. For instance,
if a user can create tables and perform inserts, he or she can execute
something like
```
CREATE TABLE cheat (revealed_ssn TEXT);
INSERT INTO cheat SELECT ssn FROM users;
SELECT revealed_ssn FROM cheat;
```
to get access to the cleartext version of a masked field `ssn`.

From MaxScale 2.3.5 onwards, the masking filter will, if any of the
`prevent_function_usage`, `check_user_variables`, `check_unions` or
`check_subqueries` parameters is set to true, block statements that
cannot be fully parsed.

Please see the configuration parameter
[require_fully_parsed](#require_fully_parsed)
for how to change the default behaviour.

From MaxScale 2.3.7 onwards, the masking filter will treat any strings
passed to functions as if they were fields. The reason is that as the
MaxScale query classifier is not aware of whether `ANSI_QUOTES` is
enabled or not, it is possible to bypass the masking by turning that
option on.
```
mysql> set @@sql_mode = 'ANSI_QUOTES';
mysql> select concat("ssn") from managers;
```
Before this change, the content of the field `ssn` would have been
returned in clear text even if the column should have been masked.

Note that this change will mean that there may be false positives
if `ANSI_QUOTES` is not enabled and a string argument happens to
be the same as the name of a field to be masked.

Please see the configuration parameter
[treat_string_arg_as_field(#treat_string_arg_as_field)
for how to change the default behaviour.

## Limitations

The masking filter can _only_ be used for masking columns of the following
types: `BINARY`, `VARBINARY`, `CHAR`, `VARCHAR`, `BLOB`, `TINYBLOB`,
`MEDIUMBLOB`, `LONGBLOB`, `TEXT`, `TINYTEXT`, `MEDIUMTEXT`, `LONGTEXT`,
`ENUM` and `SET`. If the type of the column is something else, then no
masking will be performed.

Currently, the masking filter can only work on packets whose payload is less
than 16MB. If the masking filter encounters a packet whose payload is exactly
that, thus indicating a situation where the payload is delivered in multiple
packets, the value of the parameter `large_payloads` specifies how the masking
filter should handle the situation.

## Configuration

The masking filter is taken into use with the following kind of
configuration setup.

```
[Mask-SSN]
type=filter
module=masking
rules=...

[SomeService]
type=service
...
filters=Mask-SSN
```

### Filter Parameters

The masking filter has one mandatory parameter - `rules`.

#### `rules`

Specifies the path of the file where the masking rules are stored.
A relative path is interpreted relative to the _module configuration directory_
of MariaDB MaxScale. The default module configuration directory is
_/etc/maxscale.modules.d_.

```
rules=/path/to/rules-file
```

#### `warn_type_mismatch`

With this optional parameter the masking filter can be instructed to log
a warning if a masking rule matches a column that is not of one of the
allowed types.

The values that can be used are `never` and `always`, with `never` being
the default.
```
warn_type_mismatch=always
```

#### `large_payload`

This optional parameter specifies how the masking filter should treat
payloads larger than `16MB`, that is, payloads that are delivered in
multiple MySQL protocol packets.

The values that can be used are `ignore`, which means that columns in
such payloads are not masked, and `abort`, which means that if such
payloads are encountered, the client connection is closed. The default
is `abort`.

Note that the aborting behaviour is applied only to resultsets that
contain columns that should be masked. There are *no* limitations on
resultsets that do not contain such columns.
```
large_payload=ignore
```

#### `prevent_function_usage`

This optional parameter specifies how the masking filter should behave
if a column that should be masked, is used in conjunction with some
function. As the masking filter works _only_ on the basis of the
information in the returned result-set, if the name of a column is
not present in the result-set, then the masking filter cannot mask a
value. This means that the masking filter basically can be bypassed
with a query like:
```
SELECT CONCAT(masked_column) FROM tbl;
```
If the value of `prevent_function_usage` is `true`, then all
statements that contain functions referring to masked columns will
be rejected. As that means that also queries using potentially
harmless functions, such as `LENGTH(masked_column)`, are rejected
as well, this feature can be turned off. In that case, the firewall
filter should be setup to allow or reject the use of certain functions.
```
prevent_function_usage=false
```
The default value is `true`.

#### `require_fully_parsed`

This optional parameter specifies how the masking filter should
behave in case any of `prevent_function_usage`, `check_user_variables`,
`check_unions` or `check_subqueries` is true and it encounters a
statement that cannot be fully parsed,

If true, then statements that cannot be fully parsed (due to a
parser limitation) will be blocked.
```
require_fully_parsed=false
```

The default value is `true`.

Note that if this parameter is set to false, then `prevent_function_usage`,
`check_user_variables`, `check_unions` and `check_subqueries` are rendered
less effective, as it with a statement that cannot be fully parsed may be
possible to bypass the protection that they are intended to provide.

#### `treat_string_arg_as_field`

This optional parameter specifies how the masking filter should treat
strings used as arguments to functions. If true, they will be handled
as fields, which will cause fields to be masked even if `ANSI_QUOTES` has
been enabled and `"` is used instead of backtick.
```
treat_string_arg_as_field=false
```
The default value is `true`.

#### `check_user_variables`

This optional parameter specifies how the masking filter should
behave with respect to user variables. If true, then a statement like
```
set @a = (select ssn from customer where id = 1);
```
will be rejected if `ssn` is a column that should be masked.
```
check_user_variables=false
```

The default value is `true`.

#### `check_unions`

This optional parameter specifies how the masking filter should
behave with respect to UNIONs. If true, then a statement like
```
SELECT a FROM t1 UNION select b from t2;
```
will be rejected if `b` is a column that should be masked.
```
check_unions=false
```

The default value is `true`.

#### `check_subqueries`

This optional parameter specifies how the masking filter should
behave with respect to subqueries. If true, then a statement like
```
SELECT * FROM (SELECT a as b FROM t1) as t2;
```
will be rejected if `a` is a column that should be masked.
```
check_subqueries=false
```

The default value is `true`.

## Rules

The masking rules are expressed as a JSON object.

The top-level object is expected to contain a key `rules` whose
value is an array of rule objects.
```
{
    "rules": [ ... ]
}
```

Each rule in the rules array is a JSON object, expected to
contain the keys `replace`, `with`, `applies_to` and
`exempted`. The two former ones are obligatory and the two
latter ones optional.

```
{
    "rules": [
        {
            "replace": { ... },
            "with": { ... },
            "applies_to": [ ... ],
            "exempted": [ ... ]
        }
    ]
}
```

### `replace`

The value of this key is an object that specifies the column
whose values should be masked. The object must contain the key
`column` and may contain the keys `table` and `database`. The
value of these keys must be a string.

If only `column` is specified, then a column with that name
matches irrespective of the table and database. If `table`
is specified, then the column matches only if it is in a table
with the specified name, and if `database` is specified when
the column matches only if it is in a database with the
specified name.

```
{
    "rules": [
        {
            "replace": {
                "database": "db1",
                "table": "person",
                "column": "ssn"
            },
            "with": { ... },
            "applies_to": [ ... ],
            "exempted": [ ... ]
        }
    ]
}

```

**NOTE** If a rule contains a table/database then if the resultset
does _not_ contain table/database information, it will always be
considered a match if the column matches. For instance, given the
rule above, if there is a table `person2`, also containing an `ssn`
field, then a query like
```
SELECT ssn FROM person2;
```
will not return masked values, but a query like
```
SELECT ssn FROM person UNION SELECT ssn FROM person2;
```
will _only_ return masked values, even if the `ssn` values from
`person2` in principle should not be masked. The same effect is
observed even with a nonsensical query like
```
SELECT ssn FROM person2 UNION SELECT ssn FROM person2;
```
even if nothing from `person2` should be masked. The reason is that
as the resultset contains no table information, the values must be
masked if the column name matches, as otherwise the masking could
easily be circumvented with a query like
```
SELECT ssn FROM person UNION SELECT ssn FROM person;
```

The optional key `match` makes partial replacement of the original
value possible: only the matched part would be replaced
with the fill character.
The `match` value must be a valid pcre2 regular expression.

```

            "replace": {
                "column": "ssn",
                "match": "(123)"
            },
            "with": {
                "fill": "X#"
            }
```

### `obfuscate`

The obfuscate rule allows the obfuscation of the value
by passing it through an obfuscation algorithm.
Current solution uses a non-reversible obfuscation approach.

However, note that although it is in principle impossible to obtain the
original value from the obfuscated one, if the range of possible original
values is limited, it is straightforward to figure out the possible
original values by running all possible values through the obfuscation
algorithm and then comparing the results.

The minimal configuration is:

```
            "obfuscate": {
                "column": "name"
            }

```

Output example for Db field `name` = 'remo'

```
SELECT name from db1.tbl1;`

+------+
| name |
+------+
| $-~) |
+------+
```

### `with`

The value of this key is an object that specifies what the value of the matched
column should be replaced with for the `replace` rule. Currently, the object
is expected to contain either the key `value` or the key `fill`.
The value of both must be a string with length greater than zero.
If both keys are specified, `value` takes precedence.
If `fill` is not specified, the default `X` is used as its value.

If `value` is specified, then its value is used to replace the actual value
verbatim and the length of the specified value must match the actual returned
value (from the server) exactly. If the lengths do not match, the value of
`fill` is used to mask the actual value.

When the value of `fill` (fill-value) is used for masking the returned value,
the fill-value is used as many times as necessary to match the length of the
return value. If required, only a part of the fill-value may be used in the end
of the mask value to get the lengths to match.

```
{
    "rules": [
        {
            "replace": {
                "column": "ssn"
            },
            "with": {
                "value": "XXX-XX-XXXX"
            },
            "applies_to": [ ... ],
            "exempted": [ ... ]
        },
        {
            "replace": {
                "column": "age"
            },
            "with": {
                "fill": "*"
            },
            "applies_to": [ ... ],
            "exempted": [ ... ]
        },
        {
            "replace": {
                "column": "creditcard"
            },
            "with": {
                "value": "1234123412341234",
                "fill": "0"
            },
            "applies_to": [ ... ],
            "exempted": [ ... ]
        },
    ]
}
```

### `applies_to`

With this _optional_ key, whose value must be an array of strings,
it can be specified what users the rule is applied to. Each string
should be a MariaDB account string, that is, `%` is a wildcard.

```
{
    "rules": [
        {
            "replace": { ... },
            "with": { ... },
            "applies_to": [ "'alice'@'host'", "'bob'@'%'" ],
            "exempted": [ ... ]
        }
    ]
}
```

If this key is not specified, then the masking is performed for all
users, except the ones exempted using the key `exempted`.

### `exempted`

With this _optional_ key, whose value must be an array of strings,
it can be specified what users the rule is *not* applied to. Each
string should be a MariaDB account string, that is, `%` is a wildcard.

```
{
    "rules": [
        {
            "replace": { ... },
            "with": { ... },
            "applies_to": [ ... ],
            "exempted": [ "'admin'" ]
        }
    ]
}
```

## Module commands

Read [Module Commands](../Reference/Module-Commands.md) documentation for details
about module commands.

The masking filter supports the following module commands.

### `reload`

Reload the rules from the rules file. The new rules are taken into use
only if the loading succeeds without any errors.
```
MaxScale> call command masking reload MyMaskingFilter
```
`MyMaskingFilter` refers to a particular filter section in the
MariaDB MaxScale configuration file.

## Example

In the following we configure a masking filter _MyMasking_ that should always log a
warning if a masking rule matches a column that is of a type that cannot be masked,
and that should abort the client connection if a resultset package is larger than
16MB. The rules for the masking filter are in the file `masking_rules.json`.

### Configuration
```
[MyMasking]
type=filter
module=masking
warn_type_mismatch=always
large_payload=abort
rules=masking_rules.json

[MyService]
type=service
...
filters=MyMasking
```

### `masking_rules.json`

The rules specify that the data of a column whose name is `ssn`, should
be replaced with the string _012345-ABCD_. If the length of the data is
not exactly the same as the length of the replacement value, then the
data should be replaced with as many _X_ characters as needed.
```
{
    "rules": [
        {
            "replace": {
                "column": "ssn"
            },
            "with": {
                "value": "012345-ABCD",
                "fill": "X"
            }
        }
    ]
}
```
