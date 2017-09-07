/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
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
%token FWTOK_RULE FWTOK_USERS FWTOK_RULES FWTOK_ANY FWTOK_ALL
%token FWTOK_STRICT_ALL FWTOK_MATCH FWTOK_WILDCARD FWTOK_COLUMNS FWTOK_REGEX
%token FWTOK_LIMIT_QUERIES FWTOK_WHERE_CLAUSE FWTOK_AT_TIMES FWTOK_ON_QUERIES
%token FWTOK_FUNCTION FWTOK_USES_FUNCTION FWTOK_COMMENT FWTOK_PIPE

/** Terminal typed symbols */
%token <floatval>FWTOK_FLOAT <strval>FWTOK_TIME <strval>FWTOK_BTSTR
%token <strval>FWTOK_QUOTEDSTR <strval>FWTOK_STR <strval>FWTOK_USER
%token <strval>FWTOK_CMP <strval>FWTOK_SQLOP <intval>FWTOK_INT <strval>FWTOK_RULENAME

/** Non-terminal symbols */
%type <strval>rulename
%type <strval>cond
%type <strval>orlist

%%

input
    : line_input
    | line_input command { MXS_WARNING("Firewall rules file lacks a trailing newline."); }
    ;

line_input
    :
    | line_input line
    ;

line
    : '\n'
    | command '\n'
    ;

command
    : rule
    | user
    | FWTOK_COMMENT
    ;

rule
    : FWTOK_RULE rulename {if (!set_rule_name(scanner, $2)){YYERROR;}} FWTOK_MATCH ruleparams
    ;

ruleparams
    : mandatory optional optional
    | mandatory optional
    | mandatory
    | {define_basic_rule(scanner);} optional
    ;

rulename
    : FWTOK_RULENAME
    | FWTOK_STR
    ;

user
    : FWTOK_USERS userlist FWTOK_MATCH cond FWTOK_RULES namelist
        {if (!create_user_templates(scanner)){YYERROR;}}
    ;

uservalue
    : FWTOK_USER {add_active_user(scanner, $1);}
    ;

userlist
    : uservalue
    | userlist uservalue
    ;

namevalue
    : rulename {add_active_rule(scanner, $1);}
    ;

namelist
    : namevalue
    | namelist namevalue
    ;

cond
    : FWTOK_ANY {set_matching_mode(scanner, FWTOK_MATCH_ANY);}
    | FWTOK_ALL {set_matching_mode(scanner, FWTOK_MATCH_ALL);}
    | FWTOK_STRICT_ALL {set_matching_mode(scanner, FWTOK_MATCH_STRICT_ALL);}
    ;

mandatory
    : FWTOK_WILDCARD {define_wildcard_rule(scanner);}
    | FWTOK_WHERE_CLAUSE {define_where_clause_rule(scanner);}
    | FWTOK_LIMIT_QUERIES FWTOK_INT FWTOK_INT FWTOK_INT
        {define_limit_queries_rule(scanner, $2, $3, $4);}
    | FWTOK_REGEX FWTOK_QUOTEDSTR {define_regex_rule(scanner, $2);}
    | FWTOK_COLUMNS valuelist {define_columns_rule(scanner);}
    | FWTOK_FUNCTION valuelist {define_function_rule(scanner);}
    | FWTOK_FUNCTION {define_function_rule(scanner);}
    | FWTOK_FUNCTION valuelist FWTOK_COLUMNS auxiliaryvaluelist {define_column_function_rule(scanner);}
    | FWTOK_USES_FUNCTION valuelist {define_function_usage_rule(scanner);}
    ;

value
    : FWTOK_CMP {push_value(scanner, $1);}
    | FWTOK_STR {push_value(scanner, $1);}
    | FWTOK_BTSTR  {push_value(scanner, $1);}
    ;

valuelist
    : value
    | valuelist value
    ;

auxiliaryvalue
    : FWTOK_CMP {push_auxiliary_value(scanner, $1);}
    | FWTOK_STR {push_auxiliary_value(scanner, $1);}
    | FWTOK_BTSTR  {push_auxiliary_value(scanner, $1);}
    ;

auxiliaryvaluelist
    : auxiliaryvalue
    | auxiliaryvaluelist auxiliaryvalue
    ;

/** Optional parts of a rule */
optional
    : FWTOK_AT_TIMES timelist
    | FWTOK_ON_QUERIES orlist
    ;

timelist
    : FWTOK_TIME {if (!add_at_times_rule(scanner, $1)){YYERROR;}}
    | timelist FWTOK_TIME {if (!add_at_times_rule(scanner, $2)){YYERROR;}}
    ;

orlist
    : FWTOK_SQLOP {add_on_queries_rule(scanner, $1);}
    | orlist FWTOK_PIPE FWTOK_SQLOP {add_on_queries_rule(scanner, $3);}
    ;
