# Masking

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
+-------+------------+
+ name  | ssn        |
+-------+------------+
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

Note that he masking filter alone is *not* sufficient for preventing
access to a particular column. As the masking filter works on the column
name alone a query like
```
> SELECT name, concat(ssn) FROM person;
```
will reveal the value. Also, executing a query like
```
> SELECT name FROM person WHERE ssn = ...;
```
a sufficient number of times with different _ssn_ values, will, eventually,
reveal the social security number of all persons in the database.

For a secure solution, the masking filter *must* be combined with the
firewall filter to prevent the use of functions and the use of particular
columns in where-clauses.

## Limitations

The masking filter can _only_ be used for masking columns of the following
types: `BINARY`, `VARBINARY`, `CHAR`, `VARCHAR`, 'BLOB', TINYBLOB`,
`MEDIUMBLOB`, `LONGBLOB`, `TEXT`, `TINYTEXT`, `MEDIUMTEXT`, `LONGTEXT`,
`ENUM` and `SET`.

If the type of the column is something else, then no masking will be performed.

## Configuration

The masking filter is taken into use with the following kind of
configuration setup.

```
[Mask-SSN]
type=filter
module=masking
rules_file=...

[SomeService]
type=service
...
filters=Mask-SSN
```

## Filter Parameters

The masking filter has one mandatory parameter - `rules_file`.

#### `rules_file`

Specifies the path of the file where the masking rules are stored.
A relative path is interpreted relative to the _data directory_ of
MariaDB MaxScale.

```
rules_file=/path/to/rules-file
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

# Rules

The masking rules are expressed as a JSON object.

The top-level object is expected to contain a key `rules` whose
value is an array of rule objects.
```
{
    "rules": [ ... ]
}
```

## Rule

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

#### `replace`

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

#### `with`

The value of this key is an object that specifies what the value
of the matched column should be replaced with. Currently, the object
is expected to contain either the key `value` or the key `fill`. The
value of both must be a string. If both keys are specified, then
`value` takes presedence.

If `value` is specified, then its value is used to replace the actual
value verbatim and the length of the specified value must match the
actual returned value (from the server) exactly. If the lengths do
not match, then if `fill` is specified its value will be used to
mask the actual value. Otherwise an error is logged and the value
is *not* masked.

If `fill` is specified, then its value will be used for masking the
value; as such if the lenghts match, by cutting it if the actual value
is shorter, and by repeating it, fully or partially, the necessary
amount of times, if the actual value is longer.
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
                "value": "1234123412341234"
                "fill": "0"
            },
            "applies_to": [ ... ],
            "exempted": [ ... ]
        },
    ]
}
```

#### `applies_to`

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

#### `exempted`

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

Read [Module Commands](../Reference/Module-Commands.md) documentation for details about module commands.

The masking filter supports the following module commands.

### `reload`

Reload the rules from the rules file. The new rules are taken into use
only if the loading succeeds without any errors.
```
MaxScale> call command masking reload MyMaskingFilter
```
`MyMaskingFilter` refers to a particular filter section in the
MariaDB MaxScale configuration file.
