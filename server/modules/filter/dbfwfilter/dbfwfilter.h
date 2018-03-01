#pragma once
#ifndef _DBFWFILTER_H
#define _DBFWFILTER_H
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

/**
 * @file dbfwfilter.h - Database firewall filter
 *
 * External functions used by the rule parser. These functions are called from
 * the generater parser which is created from the ruleparser.y and token.l files.
 */

#include <maxscale/cdefs.h>
#include <stdbool.h>

MXS_BEGIN_DECLS

/** Matching type */
enum match_type
{
    FWTOK_MATCH_ANY,
    FWTOK_MATCH_ALL,
    FWTOK_MATCH_STRICT_ALL
};

/** Prototype for the parser's error handler */
void dbfw_yyerror(void* scanner, const char* error);

/** Rule creation and definition functions */
bool create_rule(void* scanner, const char* name);
void define_wildcard_rule(void* scanner);
void define_where_clause_rule(void* scanner);
bool define_regex_rule(void* scanner, char* pattern);
bool define_columns_rule(void* scanner, char* columns);
bool define_function_rule(void* scanner, char* columns);
bool define_limit_queries_rule(void* scanner, int max, int timeperiod, int holdoff);
bool add_at_times_rule(void* scanner, const char* range);
void add_on_queries_rule(void* scanner, const char* sql);

/** User creation functions */
bool add_active_user(void* scanner, const char* name);
bool add_active_rule(void* scanner, const char* name);
void set_matching_mode(void* scanner, enum match_type mode);
bool create_user_templates(void* scanner);

MXS_END_DECLS

#endif
