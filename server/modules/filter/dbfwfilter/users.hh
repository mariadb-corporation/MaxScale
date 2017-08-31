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
#include "rules.hh"

/**
 * A temporary template structure used in the creation of actual users.
 * This is also used to link the user definitions with the rules.
 * @see struct user_t
 */
struct UserTemplate
{
    std::string     name;      /** Name of the user */
    enum match_type type;      /** Matching type */
    ValueList       rulenames; /** Names of the rules */
};

typedef std::tr1::shared_ptr<UserTemplate> SUserTemplate;
typedef std::list<SUserTemplate>           TemplateList;

/**
 * A user definition
 */
class User
{
    User(const User&);
    User& operator=(const User&);

public:
    User(std::string name):
        name(name),
        lock(SPINLOCK_INIT),
        qs_limit(NULL)
    {
    }

    ~User()
    {
        delete qs_limit;
    }

    std::string name;             /*< Name of the user */
    SPINLOCK    lock;             /*< User spinlock */
    QUERYSPEED* qs_limit;         /*< The query speed structure unique to this user */
    RuleList    rules_or;         /*< If any of these rules match the action is triggered */
    RuleList    rules_and;        /*< All of these rules must match for the action to trigger */
    RuleList    rules_strict_and; /*< rules that skip the rest of the rules if one of them
                                   * fails. This is only for rules paired with 'match strict_all'. */
};

typedef std::tr1::shared_ptr<User>                  SUser;
typedef std::tr1::unordered_map<std::string, SUser> UserMap;
