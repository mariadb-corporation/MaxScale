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
    UserTemplate(std::string name, const ValueList& rules, match_type mode):
        name(name),
        type(mode),
        rulenames(rules)
    {
    }

    std::string name;      /** Name of the user */
    match_type  type;      /** Matching type */
    ValueList   rulenames; /** Names of the rules */
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
    User(std::string name);
    ~User();

    /**
     * Get the name of this user
     *
     * @return Name of the user
     */
    const char* name() const;

    /**
     * Add new rules to existing rules
     *
     * @param mode  Matching mode for the rule
     * @param rules Rules to append
     */
    void add_rules(match_type mode, const RuleList& rules);

    /**
     * Check if a query matches some rule
     *
     * @param instance Filter instance
     * @param session  Filter session
     * @param buffer   Buffer containing the query
     * @param rulename Names of rules that this query matched
     *
     * @return True if query matches
     */
    bool match(Dbfw* instance, DbfwSession* session, GWBUF* buffer, char** rulename);

private:

    enum match_mode
    {
        ALL,
        STRICT
    };

    typedef std::vector<RuleList> RuleListVector;

    RuleListVector rules_or_vector;        /*< If any of these rules match the action is triggered */
    RuleListVector rules_and_vector;       /*< All of these rules must match for the action to trigger */
    RuleListVector rules_strict_and_vector;/*< rules that skip the rest of the rules if one of them
                                            * fails. This is only for rules paired with 'match strict_all'. */
    std::string m_name;                    /*< Name of the user */

    /**
     * Functions for matching rules
     */
    bool match_any(Dbfw* my_instance, DbfwSession* my_session,
                   GWBUF *queue, char** rulename);
    bool do_match(Dbfw* my_instance, DbfwSession* my_session,
                  GWBUF *queue, match_mode mode, char** rulename);
};

typedef std::tr1::shared_ptr<User>                  SUser;
typedef std::tr1::unordered_map<std::string, SUser> UserMap;
