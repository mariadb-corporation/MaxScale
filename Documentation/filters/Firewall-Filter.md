#Firewall filter

## Overview
The firewall filter is used to block queries that match a set of rules. It can be used to prevent harmful queries into the database or to limit the access to the database based on a more defined set of rules compared to the traditional GRANT-based rights management.

## Configuration

The firewall filter only requires a minimal set of configurations in the MaxScale.cnf file. The actual rules of the firewall filter are located in a separate text file. The following is an example of a firewall filter configuration in the MaxScale.cnf file.


    [Firewall]
    type=filter
    module=fwfilter
    rules=/home/user/rules.txt

### Filter Options

The firewall filter does not support any filter options.

### Filter Parameters

The firewall filter has one mandatory parameter that defines the location of the rule file. This is the `rules` parameter and it expects an absolute path to the rule file.

## Rule syntax

The rules are defined by using the following syntax.

`         rule NAME deny [wildcard | columns VALUE ... |
          regex REGEX | limit_queries COUNT TIMEPERIOD HOLDOFF |
          no_where_clause] [at_times VALUE...] [on_queries [select|update|insert|delete]]`

Rules always define a blocking action so the basic mode for the firewall filter is to allow all queries that do not match a given set of rules. Rules are identified by their name and have a mandatory part and optional parts.

The first step of defining a rule is to start with the keyword `rule` which identifies this line of text as a rule. The second token is identified as the name of the rule. After that the mandatory token `deny` is required to mark the start of the actual rule definition.

### Mandatory rule parameters

The firewall filter's rules expect a single mandatory parameter for a rule. You can define multiple rules to cover situations where you would like to apply multiple mandatory rules to a query.

#### Wildcard

This rule blocks all queries that use the wildcard character *.

#### Columns

This rule expects a list of values after the `columns` keyword. These values are interpreted as column names and if a query targets any of these, it is blocked.

#### Regex

This rule blocks all queries matching a regex enclosed in single or double quotes.

#### Limit_queries

The limit_queries rule expects three parameters. The first parameter is the number of allowed queries during the time period. The second is the time period in seconds and the third is the amount of time for which the rule is considered active and blocking.

#### No_where_clause

This rule inspects the query and blocks it if it has no where clause. This way you can't do a DELETE FROM ... query without having the where clause. This does not prevent wrongful usage of the where clause e.g. DELETE FROM ... WHERE 1=1.

### Optional rule parameters

Each mandatory rule accepts one or more optional parameters. These are to be defined after the mandatory part of the rule.

#### At_times

This rule expects a list of time ranges that define the times when the rule in question is active. The time formats are expected to be ISO-8601 compliant and to be separated by a single dash (the - character). For example defining the active period of a rule to be 17:00 to 19:00 you would add `at times 17:00:00-19:00:00` to the end of the rule.

#### On_queries

This limits the rule to be active only on certain types of queries.

### Applying rules to users

To apply the defined rules to users use the following syntax.

`users NAME ... match [any|all|strict_all] rules RULE ...`

The first keyword is users which identifies this line as a user definition line. After this a list of user names and network addresses in the format `user@0.0.0.0` is expected. The first part is the user name and the second part is the network address. You can use the `%` character as the wildcard to enable user name matching from any address or network matching for all users. After the list of users and networks the keyword match is expected. 

After this either the keyword `any` `all` or `strict_all` is expected. This defined how the rules are matched. If `any` is used when the first rule is matched the query is considered blocked and the rest of the rules are skipped. If instead the `all` keyword is used all rules must match for the query to be blocked. The `strict_all` is the same as `all` but it checks the rules from left to right in the order they were listed. If one of these does not match, the rest of the rules are not checked. This could be usedful in situations where you would for example combine `limit_queries` and `regex` rules. By using `strict_all` you can have the `regex` rule first and the `limit_queries` rule second. This way the rule only matches if the `regex` rule matches enough times for the `limit_queries` rule to match.

After the matching part comes the rules keyword after which a list of rule names is expected. This allows reusing of the rules and enables varying levels of query restriction.

## Examples

### Example rule file

The following is an example of a rule file which defines six rules and applies them to three sets of users. This rule file is used in all of the examples.

    rule block_wildcard deny wildcard at_times 8:00:00-17:00:00
    rule no_personal_info deny columns phone salary address on_queries select|delete at_times  12:00:00-18:00:00
    rule simple_regex deny regex '.*insert.*into.*select.*'
    rule dos_block deny limit_queries 10000 1.0 500.0 at_times 12:00:00-18:00:00
    rule safe_delete deny no_where_clause on_queries delete
    rule managers_table deny regex '.*from.*managers.*'
    users John@% Jane@% match any rules no_personal_info block_wildcard
    users %@80.120.% match any rules block_wildcard dos_block
    users %@% match all rules safe_delete managers_table

### Example 1 - Deny access to personal information and prevent huge queries during peak hours

Assume that a database cluster with tables that have a large number of columns is under heavy load during certain times of the day. Now also assume that large selects and querying of personal information creates unwanted stress on the cluster. Now we wouldn't want to completely prevent all the users from accessing personal information or performing large select queries, we only want to block the users John and Jane.

This can be achieved by creating two rules. One that blocks the usage of the wildcard and one that prevents queries that target a set of columns. To apply these rules to the users we define a users line into the rule file with both the rules and all the users we want to apply the rules to. The rules are defined in the example rule file on line 1 and 2 and the users line is defined on line 7.

### Example 2 - Only safe deletes into the managers table

We want to prevent accidental deletes into the managers table where the where clause is missing. This poses a problem, we don't want to require all the delete queries to have a where clause. We only want to prevent the data in the managers table from being deleted without a where clause.

To achieve this, we need two rules. The first rule can be seen on line 5 in the example rule file. This defines that all delete operations must have a where clause. This rule alone does us no good so we need a second one. The second rule is defined on line 6 and it blocks all queries that match the provided regular expression. When we combine these two rules we get the result we want. You can see the application of these rules on line 9 of the example rule file. The usage of the `all` and `strict_all` matching mode requires that all the rules must match for the query to be blocked. This in effect combines the two rules into a more complex rule.
