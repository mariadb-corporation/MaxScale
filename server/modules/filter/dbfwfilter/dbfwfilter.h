#ifndef _DBFWFILTER_H
#define _DBFWFILTER_H

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

/**
 * @file dbfwfilter.h - Database firewall filter
 *
 * External functions used by the rule parser. These functions are called from
 * the generater parser which is created from the ruleparser.y and token.l files.
 */

#include <stdbool.h>

#ifndef YYSTYPE
#define YYSTYPE DBFW_YYSTYPE
#endif

/** Matching type */
enum match_type
{
    MATCH_ANY,
    MATCH_ALL,
    MATCH_STRICT_ALL
};

/** Prototype for the parser's error handler */
void dbfw_yyerror(void* scanner, const char* error);

/** Rule creation and definition functions */
bool create_rule(void* scanner, const char* name);
void define_wildcard_rule(void* scanner);
void define_where_clause_rule(void* scanner);
bool define_regex_rule(void* scanner, char* pattern);
bool define_columns_rule(void* scanner, char* columns);
bool define_limit_queries_rule(void* scanner, int max, int timeperiod, int holdoff);
bool add_at_times_rule(void* scanner, const char* range);
void add_on_queries_rule(void* scanner, const char* sql);

/** User creation functions */
bool add_active_user(void* scanner, const char* name);
bool add_active_rule(void* scanner, const char* name);
void set_matching_mode(void* scanner, enum match_type mode);
bool create_user_templates(void* scanner);

#endif
