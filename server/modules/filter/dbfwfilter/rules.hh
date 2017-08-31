#pragma once
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

#include "dbfwfilter.hh"

/**
 * A structure used to identify individual rules and to store their contents
 *
 * Each type of rule has different requirements that are expressed as void pointers.
 * This allows to match an arbitrary set of rules against a user.
 */
struct Rule
{
    Rule(std::string name);
    virtual ~Rule();
    virtual bool matches_query(GWBUF* buffer, char** msg);
    virtual bool need_full_parsing(GWBUF* buffer) const;
    bool matches_query_type(GWBUF* buffer);

    void*          data;          /*< Actual implementation of the rule */
    std::string    name;          /*< Name of the rule */
    ruletype_t     type;          /*< Type of the rule */
    uint32_t       on_queries;    /*< Types of queries to inspect */
    int            times_matched; /*< Number of times this rule has been matched */
    TIMERANGE*     active;        /*< List of times when this rule is active */
};

typedef std::tr1::shared_ptr<Rule> SRule;
typedef std::list<SRule>           RuleList;
