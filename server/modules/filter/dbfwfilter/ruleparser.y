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
%pure-parser

/** Prefix all functions */
%name-prefix="dbfw_yy"

/** The pure parser requires one extra parameter */
%parse-param {void* scanner}
%lex-param {void* scanner}

/** Terminal symbols */
%token FWTOK_RULE <strval>FWTOK_RULENAME FWTOK_USERS <strval>FWTOK_USER FWTOK_RULES FWTOK_MATCH FWTOK_ANY FWTOK_ALL FWTOK_STRICT_ALL FWTOK_DENY
%token FWTOK_WILDCARD FWTOK_COLUMNS FWTOK_REGEX FWTOK_LIMIT_QUERIES FWTOK_WHERE_CLAUSE FWTOK_AT_TIMES FWTOK_ON_QUERIES
%token <strval>FWTOK_SQLOP FWTOK_COMMENT <intval>FWTOK_INT <floatval>FWTOK_FLOAT FWTOK_PIPE <strval>FWTOK_TIME
%token <strval>FWTOK_BTSTR <strval>FWTOK_QUOTEDSTR <strval>FWTOK_STR

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
    | FWTOK_COMMENT '\n'
    ;

rule:
    FWTOK_RULE rulename {if (!create_rule(scanner, $2)){YYERROR;}} FWTOK_DENY ruleparams
    ;

ruleparams:
    mandatory optional optional
    | mandatory optional
    | mandatory
    | optional
    ;

rulename:
    FWTOK_RULENAME
    | FWTOK_STR
    ;

user:
    FWTOK_USERS userlist FWTOK_MATCH cond FWTOK_RULES namelist
        {if (!create_user_templates(scanner)){YYERROR;}}
    ;

userlist:
    FWTOK_USER {if (!add_active_user(scanner, $1)){YYERROR;}}
    | userlist FWTOK_USER {if (!add_active_user(scanner, $2)){YYERROR;}}
    ;

namelist:
    rulename {if (!add_active_rule(scanner, $1)){YYERROR;}}
    | namelist rulename {if (!add_active_rule(scanner, $2)){YYERROR;}}
    ;

cond:
    FWTOK_ANY {set_matching_mode(scanner, FWTOK_MATCH_ANY);}
    | FWTOK_ALL {set_matching_mode(scanner, FWTOK_MATCH_ALL);}
    | FWTOK_STRICT_ALL {set_matching_mode(scanner, FWTOK_MATCH_STRICT_ALL);}
    ;

mandatory:
    FWTOK_WILDCARD {define_wildcard_rule(scanner);}
    | FWTOK_WHERE_CLAUSE {define_where_clause_rule(scanner);}
    | FWTOK_LIMIT_QUERIES FWTOK_INT FWTOK_INT FWTOK_INT
        {if (!define_limit_queries_rule(scanner, $2, $3, $4)){YYERROR;}}
    | FWTOK_REGEX FWTOK_QUOTEDSTR {if (!define_regex_rule(scanner, $2)){YYERROR;}}
    | FWTOK_COLUMNS columnlist
    ;

columnlist:
    FWTOK_BTSTR {if (!define_columns_rule(scanner, $1)){YYERROR;}}
    | FWTOK_STR {if (!define_columns_rule(scanner, $1)){YYERROR;}}
    | columnlist FWTOK_BTSTR {if (!define_columns_rule(scanner, $2)){YYERROR;}}
    | columnlist FWTOK_STR {if (!define_columns_rule(scanner, $2)){YYERROR;}}
    ;

optional:
    FWTOK_AT_TIMES timelist
    | FWTOK_ON_QUERIES orlist
    ;

timelist:
    FWTOK_TIME {if (!add_at_times_rule(scanner, $1)){YYERROR;}}
    | timelist FWTOK_TIME {if (!add_at_times_rule(scanner, $2)){YYERROR;}}
    ;

orlist:
    FWTOK_SQLOP {add_on_queries_rule(scanner, $1);}
    | orlist FWTOK_PIPE FWTOK_SQLOP {add_on_queries_rule(scanner, $3);}
    ;
