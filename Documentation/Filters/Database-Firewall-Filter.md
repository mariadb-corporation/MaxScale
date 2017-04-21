#Database Firewall filter

## Overview

The Database Firewall filter is used to block queries that match a set of
rules. It can be used to prevent harmful queries from reaching the backend
database instances or to limit access to the database based on a more flexible
set of rules compared to the traditional GRANT-based privilege system. Currently
the filter does not support multi-statements.

## Configuration

The Database Firewall filter only requires minimal configuration in the
maxscale.cnf file. The actual rules of the Database Firewall filter are located
in a separate text file. The following is an example of a Database Firewall
filter configuration in maxscale.cnf.

```
[DatabaseFirewall]
type=filter
module=dbfwfilter
rules=/home/user/rules.txt

[Firewalled Routing Service]
type=service
router=readconnrouter
servers=server1
user=myuser
passwd=mypasswd
filters=DatabaseFirewall
```

### Filter Parameters

The Database Firewall filter has one mandatory parameter, `rules`.

#### `rules`

A path to a file with the rule definitions in it. The file should be readable by
the user MariaDB MaxScale is run with. If a relative path is given, the path is
interpreted relative to the module configuration directory. The default module
configuration directory is _/etc/maxscale.modules.d_.

#### `action`

This parameter is optional and determines what action is taken when a query
matches a rule. The value can be either `allow`, which allows all matching
queries to proceed but blocks those that don't match, or `block`, which blocks
all matching queries, or `ignore` which allows all queries to proceed.

The following statement types will always be allowed through when `action` is
set to `allow`:

 - COM_QUIT: Client closes connection
 - COM_PING: Server is pinged
 - COM_CHANGE_USER: The user is changed for an active connection
 - COM_SET_OPTION: Client multi-statements are being configured
 - COM_FIELD_LIST: Alias for the `SHOW TABLES;` query
 - COM_PROCESS_KILL: Alias for `KILL <id>;` query
 - COM_PROCESS_INFO: Alias for `SHOW PROCESSLIST;`

You can have both blacklist and whitelist functionality by configuring one
filter with `action=allow` and another one with `action=block`. You can then use
different rule files with each filter, one for blacklisting and another one for
whitelisting. After this you only have to add both of these filters to a service
in the following way.

```
[my-firewall-service]
type=service
servers=server1
router=readconnroute
user=maxuser
passwd=maxpwd
filters=dbfw-whitelist|dbfw-blacklist

[dbfw-whitelist]
type=filter
module=dbfwfilter
action=allow
rules=/home/user/whitelist-rules.txt

[dbfw-blacklist]
type=filter
module=dbfwfilter
action=block
rules=/home/user/blacklist-rules.txt
```

#### `log_match`

Log all queries that match a rule. For the `any` matching mode, the name of the
rule that matched is logged and for other matching modes, the name of the last
matching rule is logged. In addition to the rule name the matched user and the
query itself is logged. The log messages are logged at the notice level.

#### `log_no_match`

Log all queries that do not match a rule. The matched user and the query is
logged. The log messages are logged at the notice level.

## Rule syntax

The rules are defined by using the following syntax:

```
rule NAME deny { wildcard | columns VALUE... |
     regex REGEX | limit_queries COUNT TIMEPERIOD HOLDOFF |
      no_where_clause} [at_times VALUE...] [on_queries [select|update|insert|delete|grant|revoke|drop|create|alter|use|load]]
```

Rules are identified by their name and have mandatory parts and optional parts.
You can add comments to the rule files by adding the `#` character at
the beginning of the line. Trailing comments are not supported.

The first step of defining a rule is to start with the keyword `rule` which
identifies this line of text as a rule. The second token is identified as
the name of the rule. After that the mandatory action token `deny` is required
to mark the start of the actual rule definition.

The rule definition must contain exactly one mandatory rule parameter. It can
also contain one of each type of optional rule parameter.

**NOTE**
Even though the rules use the `deny` token, the action taken by the filter when
a query matches a rule is controlled _solely_ by the value of the `action`
parameter (_allow_,  _block_ or _ignore_).

### Mandatory rule parameters

The Database Firewall filter's rules expect a single mandatory parameter for a
rule. You can define multiple rules to cover situations where you would like to
apply multiple mandatory rules to a query.

#### `wildcard`

This rule blocks all queries that use the wildcard character *.

#### `columns`

This rule expects a list of values after the `columns` keyword. These values are
interpreted as column names and if a query targets any of these, it is matched.

#### `function`

This rule expects a list of values after the `function` keyword. These values
are interpreted as function names and if a query uses any of these, it is
matched. The symbolic comparison operators (`<`, `>`, `>=` etc.) are also
considered functions whereas the text versions (`NOT`, `IS`, `IS NOT` etc.) are
not considered functions.

#### `regex`

This rule blocks all queries matching a regex enclosed in single or double
quotes.  The regex string expects a PCRE2 syntax regular expression. For more
information about the PCRE2 syntax, read the [PCRE2
documentation](http://www.pcre.org/current/doc/html/pcre2syntax.html).

#### `limit_queries`

The limit_queries rule expects three parameters. The first parameter is the
number of allowed queries during the time period. The second is the time period
in seconds and the third is the amount of time for which the rule is considered
active and blocking.

**WARNING:** Using `limit_queries` in `action=allow` is not supported.

#### `no_where_clause`

This rule inspects the query and blocks it if it has no WHERE clause. For
example, this would disallow a `DELETE FROM ...` query without a `WHERE`
clause. This does not prevent wrongful usage of the `WHERE` clause e.g. `DELETE
FROM ... WHERE 1=1`.

### Optional rule parameters

Each mandatory rule accepts one or more optional parameters. These are to be
defined after the mandatory part of the rule.

#### `at_times`

This rule expects a list of time ranges that define the times when the rule in
question is active. The time formats are expected to be ISO-8601 compliant and
to be separated by a single dash (the - character). For example, to define the
active period of a rule to be 5pm to 7pm, you would include `at times
17:00:00-19:00:00` in the rule definition. The rule uses local time to check if
the rule is active and has a precision of one second.

#### `on_queries`

This limits the rule to be active only on certain types of queries. The possible
values are:

|Keyword|Matching operations           |
|-------|------------------------------|
|select |SELECT statements             |
|insert |INSERT statements             |
|update |UPDATE statements             |
|delete |DELETE statements             |
|grant  |All grant operations          |
|revoke |All revoke operations         |
|create |All create operations         |
|alter  |All alter operations          |
|drop   |All drop operations           |
|use    |USE operations                |
|load   |LOAD DATA operations          |

### Applying rules to users

The `users` directive defines the users to which the rule should be applied.

`users NAME... match { any | all | strict_all } rules RULE...`

The first keyword is `users`, which identifies this line as a user definition
line.

The second component is a list of user names and network addresses in the format
*`user`*`@`*`0.0.0.0`*. The first part is the user name and the second part is
the network address. You can use the `%` character as the wildcard to enable
user name matching from any address or network matching for all users. After the
list of users and networks the keyword match is expected.

After this either the keyword `any` `all` or `strict_all` is expected. This
defined how the rules are matched. If `any` is used when the first rule is
matched the query is considered as matched and the rest of the rules are
skipped. If instead the `all` keyword is used all rules must match for the query
to be considered as matched. The `strict_all` is the same as `all` but it checks the rules
from left to right in the order they were listed. If one of these does not
match, the rest of the rules are not checked. This could be useful in situations
where you would for example combine `limit_queries` and `regex` rules. By using
`strict_all` you can have the `regex` rule first and the `limit_queries` rule
second. This way the rule only matches if the `regex` rule matches enough times
for the `limit_queries` rule to match.

After the matching part comes the rules keyword after which a list of rule names
is expected. This allows reusing of the rules and enables varying levels of
query restriction.

## Module commands

Read [Module Commands](../Reference/Module-Commands.md) documentation for
details about module commands.

The dbfwfilter supports the following module commands.

### `dbfwfilter::rules/reload [FILE]`

Load a new rule file or reload the current rules. New rules are only taken into
use if they are successfully loaded and in cases where loading of the rules
fail, the old rules remain in use. The _FILE_ argument is an optional path to a
rule file and if it is not defined, the current rule file is used.

### `dbfwfilter::rules`

Shows the current statistics of the rules.

## Use Cases

### Use Case 1 - Prevent rapid execution of specific queries

To prevent the excessive use of a database we want to set a limit on the rate of
queries. We only want to apply this limit to certain queries that cause unwanted
behaviour. To achieve this we can use a regular expression.

First we define the limit on the rate of queries. The first parameter for the
rule sets the number of allowed queries to 10 queries and the second parameter
sets the rate of sampling to 5 seconds. If a user executes queries faster than
this, any further queries that match the regular expression are blocked for 60
seconds.

```
rule limit_rate_of_queries deny limit_queries 10 5 60
rule query_regex deny regex '.*select.*from.*user_data.*'
```

To apply these rules we combine them into a single rule by adding a `users` line
to the rule file.

```
users %@% match all rules limit_rate_of_queries query_regex
```

### Use Case 2 - Only allow deletes with a where clause

We have a table which contains all the managers of a company. We want to prevent
accidental deletes into this table where the where clause is missing. This poses
a problem, we don't want to require all the delete queries to have a where
clause. We only want to prevent the data in the managers table from being
deleted without a where clause.

To achieve this, we need two rules. The first rule defines that all delete
operations must have a where clause. This rule alone does us no good so we need
a second one. The second rule blocks all queries that match a regular
expression.

```
rule safe_delete deny no_where_clause on_queries delete
rule managers_table deny regex '.*from.*managers.*'
```

When we combine these two rules we get the result we want. To combine these two
rules add the following line to the rule file.

```
users %@% match all rules safe_delete managers_table
```
