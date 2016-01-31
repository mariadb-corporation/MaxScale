/*
 * This file is distributed as part of MaxScale by MariaDB Corporation.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2016
 */

%{
#include <ruleparser.yy.h>

/** The tokenizer needs this declared in order to avoid naming conflicts */
#ifndef YYSTYPE
#define YYSTYPE DBFW_YYSTYPE
#endif

%}

%option reentrant noyywrap bison-bridge prefix="dbfw_yy"
/* %option header-file="lex.yy.h" */

RULENAME    [-_[:alnum:][:punct:]]+
STR         [_[:alnum:]]+
TIME        [0-9]{2}:[0-9]{2}:[0-9]{2}-[0-9]{2}:[0-9]{2}:[0-9]{2}
QSTR        (\"[^\"]*\")|('[^']*')
SPACE       [[:space:]]*
COMMENT     ^[[:space:]]*#.*
INT         [0-9]+
FLOAT       [0-9]+[.][0-9]+
SQL         select|insert|update|delete
PIPE        [|]
USER        [^[:space:]]*[@]
IP          [0-9]{1,3}[.][0-9]{1,3}[.][0-9]{1,3}[.][0-9]{1,3}
BTSTR       `[^`]*`
USTR        [%-_[:alnum:][:punct:]]+

%%
"\n"+           dbfw_yyset_lineno(dbfw_yyget_lineno(yyscanner) + dbfw_yyget_leng(yyscanner), yyscanner);return '\n';
{COMMENT}       return FWTOK_COMMENT;
deny|allow      return FWTOK_DENY; /** This should be removed at some point */
rule            return FWTOK_RULE;
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
