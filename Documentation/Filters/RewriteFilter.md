# Rewrite Filter

## Overview

The rewrite filter allows modification of sql queries on the fly.
Reasons for modifying queries can be to rewrite a query for performance,
or to change a specific query when the client query is incorrect and
cannot be changed in a timely manner.

The examples will use Rewrite Filter file format. See below.

### Syntax

#### Native syntax

Rewriter native syntax uses placeholders to grab and replace parts of text.

##### Placeholders

The syntax for a plain placeholder is `@{N}` where N is a positive integer.

A placeholder regex is written `@{N:regex}`. It allows more control when needed.
Anchoring matching to the start `@{N:^}` or end `@{N:$}` of the sql to be
matched should be done when possible. This is especially important for the
start-anchor as it makes matching much faster. Under the hood the Native syntax
creates ECMAScript regular expressions so a placeholder regex must use
that grammar.

In a a placeholder regex, all escaped characters are un-escaped. This means
that special escape sequences like `\s` (space) must be written as `\\s`.
Prefer character classes `[[:space:]]` as they work for all supported
grammars. Plain parenthesis, on the other hand, are automatically escaped
and matched literally.

The below is a valid entry in rf format. For demonstration, all options are set.
This entry is a do-nothing entry, but illustrates placeholders.

```
%%
# options
regex_grammar: Native
case_sensitive: true
what_if: false
continue_if_matched: false
ignore_whitespace: true
%
# match template
@{1:^}select @{2} from my_table where id = @{3}
%
# replace template
select @{2} from my_table where id = @{3}
```

If the input sql is `select id, name from my_table where id = 42`
then `@{2} = "id, name"` and `@{3} = "42"`. Since the replace template
is identical to the match template the end result is that the output sql
will be the same as the input sql.

Placeholders can be used as forward references.
`@{1:^}select @{2}, count(*) from @{3} group by @{2}`.
For a match, the two ``@{2}`` text grabs must be equal.

###### Match template

The match template is used to match against the sql to be rewritten.

The match template can be partial `from mytable`. But the actual underlying
regex match is always for the whole sql. If the match template does not
start or end with a placeholder, placeholders are automatically added so
that the above becomes `@{1}from mytable@{2}`. The automatically added
placeholders cannot be used in the replace template.

Matching the whole input also means that Native syntax does not support
(and is not intended to support) scan and replace. Only the first occurrance
of the above `from mytable` can be modified in the replace template.
However, one can selectively choose to modify e.g. the first through
third occurrance of `from mytable` by writing
`from mytable @{1} from mytable @{2} from mytable`.

For scan and replace use a different regex_grammar (see below).

##### Replace template

The replace template uses the placeholders from the match template to
rewrite sql.

```
%%
# use default options by leaving this blank
%
@{1:^}select count(distinct @{2}) from @{3}
%
select count(*) from (select distinct @{1} from @{2}) as t123

Input: select count(distinct author) from books where entity != "AI"

Rewritten: select count(*) from (select distinct author from books where entity != "AI") as t123
```

An important option for smooth matching is `ignore_whitespace`, which
is on (true) by default. It creates the match regex in such a way that
the amount and kind of whitespace does not affect matching. However,
to make `ignore_whitespace` always work, it is important to add
whitespace where allowed. If "id=42" is in the match template then
only the exact "id=42" can match. But if "id = 42" is used, and
`ignore_whitespace` is on, both "id=42" and "id = 42" will match.

Another example, and what not to do:
```
%%
%
from mytable
%
from mytable force index (myindex)

Input: select name from mytable where id=42

Rewritten: select name from mytable force index (myindex) where id=42
```
That works, but because the match lacks specific detail about the
expected sql, things are likely to break. In this case
`show indexes from any_table` would no longer work.

The minimum detail in this case could be:
```
%%
%
@{1:^}select @{2} from mytable
%
select @{2} from mytable force index (myindex)
```
but if more detail is known, like something specific in the where clause,
that too should be added.

##### Plaholder regex

Syntax: @{N:regex}

Parenthesis and the right brace '}' have special meaning in the
rewriter processing. They can only be matched literally, and
if added to the regex, they must be escaped.
For example, match X exactly 5 times @{1:X{5\\}}.

Suppose an application is misbehaving after an upgrade and a quick fix is needed.
This query `select zip from address_book where str_id = "AZ-124"` is correct,
but if the id is an integer the where clause should be `id = 1234`.
```
%%
%
@{1:^}select zip_code from address_book where str_id = ["]@{1:[[:digit:]]+}["]
%
select zip_code from address_book where id = @{1}

Input: select zip_code from address_book where str_id = "1234"

Rewritten: select zip_code from address_book where id = 1234
```
#### Using plain regular expressions

For scan and replace the regex_grammar must be set to something else than
Native. An example will illustrate the usage.

Replace all occurrances of "wrong_table_name" with "correct_table_name".
Further, if the replacement was made then replace all occurrances
of wrong_column_name with correct_column_name.
```
%%
regex_grammar: EPosix
continue_if_matched: true
%
wrong_table_name
%
correct_table_name

%%
regex_grammar: EPosix
%
wrong_column_name
%
correct_column_name
```
## Configuration

Adding a rewrite filter.

```
[Rewrite]
type = filter
module = rewritefilter
template_file = /path/to/template_file.rf
...

[Router]
type=service
...
filters=Rewrite
```

### Parameters in maxscale.cnf

#### `template_file`

- **Type**: string
- **Mandatory**: Yes
- **Dynamic**: Yes
- **Default**: No default value

Path to the template file.

#### `regex_grammar`

- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: Native
- **Values**: `Native`, `ECMAScript`, `Posix`, `EPosix`, `Awk`, `Grep`, `EGrep`

Default regex_grammar for templates

#### `case_sensitive`

- **Type**: boolean
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: true

Default case sensitivity for templates

#### `log_replacement`

- **Type**: boolean
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

Log replacements at NOTICE level.

### Parameters per template in the template file

#### `regex_grammar`
- **Type**: string
- **Values**: `Native`, `ECMAScript`, `Posix`, `EPosix`, `Awk`, `Grep`, `EGrep`
- **Default**: From maxscale.cnf

Overrides the global regex_grammar of a template.

#### `case_sensitive`

- **Type**: boolean
- **Default**: From maxscale.cnf

Overrides the global case sensitivity of a template.

#### `ignore_whitespace`

- **Type**: boolean
- **Default**: true

Ignore whitespace differences in the match template and input sql.

#### `continue_if_matched`

- **Type**: boolean
- **Default**: false

If a template matches and the replacement is done, continue to the
next template and apply it to the result of the previous rewrite.

#### `what_if`

- **Type**: boolean
- **Default**: false

Do not make the replacement, only log what would have
been replaced (NOTICE level).

## Rewrite file format

The rf format for an entry is:
```
%%
options
%
match template
%
replace template
```
The character `#` starts a single line comment when it is the
first character on a line.

Empty lines are ignored.

Options are specified as follows:
```
case_sensitive: true
```
The colon must stick to the option name.

The separators `%` and `%%` must be the exact content of
their respective separator lines.

The templates can span multiple lines. Whitespace does not
matter as long as `ignore_whitespace = true`. Always use space
where space is allowed to maximize the utility of
`ignore_whitespace`.

Nothing needs to be escaped in rf, except when a placeholder
regex is defined, where the characters "}()" must be escaped.
For example, match X exactly 5 times @{1:X{5\\}}.

Example
```
%%
case_sensitive: false
%
@{1:^}select @{2} from mytable where user = @{3}
%
select @{2} from mytable where user = @{3}
and @{3} in (select user from approved_users)
```
## Json file format

The json file format is harder to read and edit manually.
It will be needed if support for editing of rewrite templates
is added to the GUI.

All double quotes in the templates have to be escaped.

The same example as above is:
```
{ "templates" :
    [
        {
            "case_sensitive" : false,
            "match_template" : "@{1:^}select @{2} from mytable where user = @{3}",
            "replace_template" : "select @{2} from mytable where user = @{3}
and @{3} in (select user from approved_users)"
        }
    ]
}
```
## Reload template file

The configuration is re-read if any dynamic value is updated
even if the value does not change.
```
maxctrl alter filter Rewrite log_replacement=false
```
