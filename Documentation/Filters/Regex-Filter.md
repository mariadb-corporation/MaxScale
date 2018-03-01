# Regex Filter

## Overview

The Regex filter is a filter module for MariaDB MaxScale that is able to rewrite query content using regular expression matches and text substitution. It uses the PCRE2 syntax which differs from the POSIX regular expressions used in MariaDB MaxScale versions prior to 1.3.0.

For all details about the PCRE2 syntax, please read the [PCRE2 syntax documentation](http://www.pcre.org/current/doc/html/pcre2syntax.html).

Please note that the PCRE2 library uses a different syntax to refer to capture groups in the replacement string. The main difference is the usage of the dollar character instead of the backslash character for references e.g. `$1` instead of `\1`. For more details about the replacement string differences, please read the [Creating a new string with substitutions](http://www.pcre.org/current/doc/html/pcre2api.html#SEC34) chapter in the PCRE2 manual.

## Configuration

The configuration block for the Regex filter requires the minimal filter options in its section within the maxscale.cnf file, stored in /etc/maxscale.cnf.

```
[MyRegexFilter]
type=filter
module=regexfilter
match=some string
replace=replacement string

[MyService]
type=service
router=readconnrouter
servers=server1
user=myuser
passwd=mypasswd
filters=MyRegexfilter
```

## Filter Options

The Regex filter accepts the options ignorecase or case. These define if the pattern text should take the case of the string it is matching against into consideration or not.

## Filter Parameters

The Regex filter requires two mandatory parameters to be defined.

### `match`

A parameter that can be used to match text in the SQL statement which should be replaced.

```
match=TYPE[	]*=
```

If the filter option ignorecase is used all regular expressions are evaluated with the option to ignore the case of the text, therefore a match option of select will match both type, TYPE and any form of the word with upper or lowercase characters.

### `replace`

The replace parameter defines the text that should replace the text in the SQL text which matches the match.

```
replace=ENGINE =
```

### `source`

The optional source parameter defines an address that is used to match against the address from which the client connection to MariaDB MaxScale originates. Only sessions that originate from this address will have the match and replacement applied to them.

```
source=127.0.0.1
```

### `user`

The optional user parameter defines a user name that is used to match against the user from which the client connection to MariaDB MaxScale originates. Only sessions that are connected using this username will have the match and replacement applied to them.

```
user=john
```

### `log_file`

The optional log_file parameter defines a log file in which the filter writes all queries that are not matched and matching queries with their replacement queries. All sessions will log to this file so this should only be used for diagnostic purposes.

```
log_file=/tmp/regexfilter.log
```

### `log_trace`

The optional log_trace parameter toggles the logging of non-matching and
matching queries with their replacements into the log file on the *info* level.
This is the preferred method of diagnosing the matching of queries since the
log level can be changed at runtime. For more details about logging levels and
session specific logging, please read the [Configuration Guide](../Getting-Started/Configuration-Guide.md#global-settings)
and the [MaxAdmin](../Reference/MaxAdmin.md#change-maxscale-logging-options) documentation on changing the logging levels.

```
log_trace=true
```

## Examples

### Example 1 - Replace MySQL 5.1 create table syntax with that for later versions

MySQL 5.1 used the parameter TYPE = to set the storage engine that should be used for a table. In later versions this changed to be ENGINE =. Imagine you have an application that you can not change for some reason, but you wish to migrate to a newer version of MySQL. The regexfilter can be used to transform the create table statements into the form that could be used by MySQL 5.5

```
[CreateTableFilter]
type=filter
module=regexfilter
options=ignorecase
match=TYPE\s*=
replace=ENGINE=

[MyService]
type=service
router=readconnrouter
servers=server1
user=myuser
passwd=mypasswd
filters=CreateTableFilter
```
