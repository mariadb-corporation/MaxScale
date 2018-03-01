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

%union{
    int intval;
    char* strval;
    float floatval;
}

%{
#include <lex.yy.h>
#include "dbfwfilter.h"
#include <maxscale/log_manager.h>
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
%token <strval>FWTOK_BTSTR <strval>FWTOK_QUOTEDSTR <strval>FWTOK_STR FWTOK_FUNCTION <strval>FWTOK_CMP

/** Non-terminal symbols */
%type <strval>rulename
%type <strval>cond
%type <strval>columnlist
%type <strval>orlist

%%

input:
    line_input
    | line_input command { MXS_WARNING("Firewall rules file lacks a trailing newline."); }
    ;

line_input:

    | line_input line
    ;

line:
    '\n'
    | command '\n'
    ;

command:
    rule
    | user
    | FWTOK_COMMENT
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
    | FWTOK_FUNCTION functionlist
    ;

columnlist:
    FWTOK_BTSTR {if (!define_columns_rule(scanner, $1)){YYERROR;}}
    | FWTOK_STR {if (!define_columns_rule(scanner, $1)){YYERROR;}}
    | columnlist FWTOK_BTSTR {if (!define_columns_rule(scanner, $2)){YYERROR;}}
    | columnlist FWTOK_STR {if (!define_columns_rule(scanner, $2)){YYERROR;}}
    ;

functionlist:
    functionvalue
    | functionlist functionvalue
    ;

functionvalue:
    FWTOK_CMP {if (!define_function_rule(scanner, $1)){YYERROR;}}
    | FWTOK_STR {if (!define_function_rule(scanner, $1)){YYERROR;}}
    | FWTOK_BTSTR  {if (!define_function_rule(scanner, $1)){YYERROR;}}
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
