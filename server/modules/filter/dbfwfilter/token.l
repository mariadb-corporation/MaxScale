/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

%{
#include <ruleparser.yy.h>

%}

%option reentrant noyywrap bison-bridge prefix="dbfw_yy"
%option yylineno

RULENAME    [-_[:alnum:][:punct:]]+
STR         [_[:alnum:]]+
TIME        [0-9]{2}:[0-9]{2}:[0-9]{2}-[0-9]{2}:[0-9]{2}:[0-9]{2}
QSTR        (\"[^\"]*\")|('[^']*')
SPACE       [[:space:]]*
COMMENT     ^[[:space:]]*#.*
INT         [0-9]+
FLOAT       [0-9]+[.][0-9]+
SQL         select|insert|update|delete|grant|revoke|drop|create|alter|use|load
PIPE        [|]
USER        [^[:space:]]*[@]
IP          [0-9]{1,3}[.][0-9]{1,3}[.][0-9]{1,3}[.][0-9]{1,3}
BTSTR       `[^`]*`
USTR        [%-_[:alnum:][:punct:]]+
CMP         [=<>!]+

%%
"\n"+           return '\n';
{COMMENT}       return FWTOK_COMMENT;
deny|allow      return FWTOK_DENY; /** This should be removed at some point */
rule            return FWTOK_RULE;
function        return FWTOK_FUNCTION;
no_where_clause return FWTOK_WHERE_CLAUSE;
wildcard        return FWTOK_WILDCARD;
columns         return FWTOK_COLUMNS;
regex           return FWTOK_REGEX;
limit_queries   return FWTOK_LIMIT_QUERIES;
at_times        return FWTOK_AT_TIMES;
on_queries      return FWTOK_ON_QUERIES;
users           return FWTOK_USERS;
rules           return FWTOK_RULES;
match           return FWTOK_MATCH;
any             return FWTOK_ANY;
all             return FWTOK_ALL;
strict_all      return FWTOK_STRICT_ALL;
{CMP}           yylval->strval = yytext;return FWTOK_CMP;
{USTR}@{USTR}   yylval->strval = yytext;return FWTOK_USER;
{BTSTR}@{BTSTR} yylval->strval = yytext;return FWTOK_USER;
{QSTR}@{QSTR}   yylval->strval = yytext;return FWTOK_USER;
({SQL}{SPACE}{PIPE}{SPACE})*{SQL} {
                yylval->strval = yytext;return FWTOK_SQLOP;
                }
{TIME}          yylval->strval = yytext;return FWTOK_TIME;
{PIPE}          return FWTOK_PIPE;
{INT}           yylval->intval = atoi(yytext);return FWTOK_INT;
{FLOAT}         yylval->floatval = atof(yytext);return FWTOK_FLOAT;
{QSTR}          yylval->strval = yytext;return FWTOK_QUOTEDSTR;
{BTSTR}         yylval->strval = yytext;return FWTOK_BTSTR;
{STR}           yylval->strval = yytext;return FWTOK_STR;
{RULENAME}      yylval->strval = yytext;return FWTOK_RULENAME;
{SPACE}         ;

%%
