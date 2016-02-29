#Database Firewall filter

## Overview
The database firewall filter is used to block queries that match a set of rules. It can be used to prevent harmful queries from reaching the backend database instances or to limit access to the database based on a more flexible set of rules compared to the traditional GRANT-based privilege system. Currently the filter does not support multi-statements.

## Configuration

The database firewall filter only requires minimal configuration in the maxscale.cnf file. The actual rules of the database firewall filter are located in a separate text file. The following is an example of a database firewall filter configuration in maxscale.cnf.

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

### Filter Options

The database firewall filter supports a single option, `ignorecase`. This will set the regular expression matching to case-insensitive mode.

### Filter Parameters

#### `rules`

The database firewall filter has one mandatory parameter that defines the location of the rule file. It expects an absolute path to the rule file.

#### `action`

This parameter is optional and determines what action is taken when a query matches a rule. The value can be either `allow`, which allows all matching queries to proceed but blocks those that don't match, or `block`, which blocks all matching queries, or `ignore` which allows all queries to proceed.

#### `log_match`

Log all queries that match a rule. For the `any` matching mode, the name of
the rule that matched is logged and for other matching modes, the name of
the last matching rule is logged. In addition to the rule name the matched
user and the query itself is logged. The log messages are logged at the notice level.

#### `log_no_match`

Log all queries that do not match a rule. The matched user and the query is
logged. The log messages are logged at the notice level.

## Rule syntax

The rules are defined by using the following syntax:

```
rule NAME deny [wildcard | columns VALUE ... |
     regex REGEX | limit_queries COUNT TIMEPERIOD HOLDOFF |
      no_where_clause] [at_times VALUE...] [on_queries [select|update|insert|delete]]
```

Rules always define a blocking action so the basic mode for the database firewall filter is to allow all queries that do not match a given set of rules. Rules are identified by their name and have a mandatory part and optional parts. You can add comments to the rule files by adding the `#` character at the beginning of the line.

The first step of defining a rule is to start with the keyword `rule` which identifies this line of text as a rule. The second token is identified as the name of the rule. After that the mandatory token `deny` is required to mark the start of the actual rule definition.

### Mandatory rule parameters

The database firewall filter's rules expect a single mandatory parameter for a rule. You can define multiple rules to cover situations where you would like to apply multiple mandatory rules to a query.

#### `wildcard`

This rule blocks all queries that use the wildcard character *.

#### `columns`

This rule expects a list of values after the `columns` keyword. These values are interpreted as column names and if a query targets any of these, it is blocked.

#### `regex`

This rule blocks all queries matching a regex enclosed in single or double quotes.

#### `limit_queries`

The limit_queries rule expects three parameters. The first parameter is the number of allowed queries during the time period. The second is the time period in seconds and the third is the amount of time for which the rule is considered active and blocking.

#### `no_where_clause`

This rule inspects the query and blocks it if it has no WHERE clause. For example, this would disallow a `DELETE FROM ...` query without a `WHERE` clause. This does not prevent wrongful usage of the `WHERE` clause e.g. `DELETE FROM ... WHERE 1=1`.

### Optional rule parameters

Each mandatory rule accepts one or more optional parameters. These are to be defined after the mandatory part of the rule.

#### `at_times`

This rule expects a list of time ranges that define the times when the rule in question is active. The time formats are expected to be ISO-8601 compliant and to be separated by a single dash (the - character). For example, to define the active period of a rule to be 5pm to 7pm, you would include `at times 17:00:00-19:00:00` in the rule definition. The rule uses local time to check if the rule is active and has a precision of one second.

#### `on_queries`

This limits the rule to be active only on certain types of queries.

### Applying rules to users

The `users` directive defines the users to which the rule should be applied.

`users NAME ... match [any|all|strict_all] rules RULE [,...]`

The first keyword is `users`, which identifies this line as a user definition line.

The second component is a list of user names and network addresses in the format *`user`*`@`*`0.0.0.0`*. The first part is the user name and the second part is the network address. You can use the `%` character as the wildcard to enable user name matching from any address or network matching for all users. After the list of users and networks the keyword match is expected. 

After this either the keyword `any` `all` or `strict_all` is expected. This defined how the rules are matched. If `any` is used when the first rule is matched the query is considered blocked and the rest of the rules are skipped. If instead the `all` keyword is used all rules must match for the query to be blocked. The `strict_all` is the same as `all` but it checks the rules from left to right in the order they were listed. If one of these does not match, the rest of the rules are not checked. This could be useful in situations where you would for example combine `limit_queries` and `regex` rules. By using `strict_all` you can have the `regex` rule first and the `limit_queries` rule second. This way the rule only matches if the `regex` rule matches enough times for the `limit_queries` rule to match.

After the matching part comes the rules keyword after which a list of rule names is expected. This allows reusing of the rules and enables varying levels of query restriction.

## Use Cases

### Use Case 1 - Prevent rapid execution of specific queries

To prevent the excessive use of a database we want to set a limit on the rate of queries. We only want to apply this limit to certain queries that cause unwanted behavior. To achieve this we can use a regular expression.

First we define the limit on the rate of queries. The first parameter for the rule sets the number of allowed queries to 10 queries and the second parameter sets the rate of sampling to 5 seconds. If a user executes queries faster than this, any further queries that match the regular expression are blocked for 60 seconds.

```
rule limit_rate_of_queries deny limit_queries 10 5 60
rule query_regex deny regex '.*select.*from.*user_data.*'
```

To apply these rules we combine them into a single rule by adding a `users` line to the rule file.

```
users %@% match all rules limit_rate_of_queries query_regex
```

### Use Case 2 - Only allow deletes with a where clause

We have a table which contains all the managers of a company. We want to prevent accidental deletes into this table where the where clause is missing. This poses a problem, we don't want to require all the delete queries to have a where clause. We only want to prevent the data in the managers table from being deleted without a where clause.

To achieve this, we need two rules. The first rule defines that all delete operations must have a where clause. This rule alone does us no good so we need a second one. The second rule blocks all queries that match a regular expression.

```
rule safe_delete deny no_where_clause on_queries delete
rule managers_table deny regex '.*from.*managers.*'
```

When we combine these two rules we get the result we want. To combine these two rules add the following line to the rule file.

```
users %@% match all rules safe_delete managers_table
```
