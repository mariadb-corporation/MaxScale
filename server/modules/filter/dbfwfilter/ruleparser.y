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

%union{
    int intval;
    char* strval;
    float floatval;
}

%{
#include <lex.yy.h>
#include <dbfwfilter.h>
%}

/** We need a reentrant scanner so no global variables are used */
%define api.pure full

/** Verbose errors help figure out what went wrong */
%define parse.error verbose

%define api.token.prefix {FWTOK_}
%define api.prefix {dbfw_yy}
/** The pure parser requires one extra parameter */
%param {void* scanner}

/** Terminal symbols */
%token RULE <strval>RULENAME USERS <strval>USER RULES MATCH ANY ALL STRICT_ALL DENY
%token WILDCARD COLUMNS REGEX LIMIT_QUERIES WHERE_CLAUSE AT_TIMES ON_QUERIES
%token <strval>SQLOP COMMENT <intval>INT <floatval>FLOAT PIPE <strval>TIME
%token <strval>BTSTR <strval>QUOTEDSTR <strval>STR

/** Non-terminal symbols */
%type <strval>rulename
%type <strval>cond
%type <strval>columnlist
%type <strval>orlist

%%

input:
    | input line
    ;

line:
    '\n'
    | rule '\n'
    | user '\n'
    | COMMENT '\n'
    ;

rule:
    RULE rulename {if (!create_rule(scanner, $2)){YYERROR;}} DENY ruleparams
    ;

ruleparams:
    mandatory optional optional
    | mandatory optional
    | mandatory
    | optional
    ;

rulename:
    RULENAME
    | STR
    ;

user:
    USERS userlist MATCH cond RULES namelist
        {if (!create_user_templates(scanner)){YYERROR;}}
    ;

userlist:
    USER {if (!add_active_user(scanner, $1)){YYERROR;}}
    | userlist USER {if (!add_active_user(scanner, $2)){YYERROR;}}
    ;

namelist:
    rulename {if (!add_active_rule(scanner, $1)){YYERROR;}}
    | namelist rulename {if (!add_active_rule(scanner, $2)){YYERROR;}}
    ;

cond:
    ANY {set_matching_mode(scanner, MATCH_ANY);}
    | ALL {set_matching_mode(scanner, MATCH_ALL);}
    | STRICT_ALL {set_matching_mode(scanner, MATCH_STRICT_ALL);}
    ;

mandatory:
    WILDCARD {define_wildcard_rule(scanner);}
    | WHERE_CLAUSE {define_where_clause_rule(scanner);}
    | LIMIT_QUERIES INT INT INT
        {if (!define_limit_queries_rule(scanner, $2, $3, $4)){YYERROR;}}
    | REGEX QUOTEDSTR {if (!define_regex_rule(scanner, $2)){YYERROR;}}
    | COLUMNS columnlist
    ;

columnlist:
    BTSTR {if (!define_columns_rule(scanner, $1)){YYERROR;}}
    | STR {if (!define_columns_rule(scanner, $1)){YYERROR;}}
    | columnlist BTSTR {if (!define_columns_rule(scanner, $2)){YYERROR;}}
    | columnlist STR {if (!define_columns_rule(scanner, $2)){YYERROR;}}
    ;

optional:
    AT_TIMES timelist
    | ON_QUERIES orlist
    ;

timelist:
    TIME {if (!add_at_times_rule(scanner, $1)){YYERROR;}}
    | timelist TIME {if (!add_at_times_rule(scanner, $2)){YYERROR;}}
    ;

orlist:
    SQLOP {add_on_queries_rule(scanner, $1);}
    | orlist PIPE SQLOP {add_on_queries_rule(scanner, $3);}
    ;
