Regex Filter

# Overview

The regex filter is a filter module for MaxScale that is able to rewrite query content using regular expression matches and text substitution.

# Configuration

The configuration block for the Regex filter requires the minimal filter options in it’s section within the MaxScale.cnf file, stored in /etc/MaxScale.cnf.

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

The regex filter accepts the options ignorecase or case. These define if the pattern text should take the case of the string it is matching against into consideration or not. 

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

The optional source parameter defines an address that is used to match against the address from which the client connection to MaxScale originates. Only sessions that originate from this address will have the match and replacement applied to them.

```
source=127.0.0.1
```

### `user`

The optional user parameter defines a user name that is used to match against the user from which the client connection to MaxScale originates. Only sessions that are connected using this username will have the match and replacement applied to them.

```
user=john
```

### `log_file`

The optional log_file parameter defines a log file in which the filter writes all queries that are not mached and maching queries with their replacement queries. All sessions will log to this file so this should only be used for diagnostic purposes.

```
log_file=/tmp/regexfilter.log
```

### `log_trace`

The optional log_trace parameter toggles the logging of non-matching and matching queries with their replacements into the trace log file. This is the preferred method of diagnosing the matching of queries since the trace log can be disabled mid-session if such a need rises.

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
match=TYPE[ 	]*=
replace=ENGINE=

[MyService]
type=service
router=readconnrouter
servers=server1
user=myuser
passwd=mypasswd
filters=CreateTableFilter
```
