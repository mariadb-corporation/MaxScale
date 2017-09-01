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

#include "user.hh"

#include <maxscale/alloc.h>
#include <maxscale/modutil.h>
#include <maxscale/protocol/mysql.h>

User::User(std::string name):
    m_name(name)
{
}

User::~User()
{
}

const char* User::name() const
{
    return m_name.c_str();
}

void User::append_rules(match_type mode, const RuleList& rules)
{
    switch (mode)
    {
    case FWTOK_MATCH_ANY:
        rules_or.insert(rules_or.end(), rules.begin(), rules.end());
        break;

    case FWTOK_MATCH_ALL:
        rules_and.insert(rules_and.end(), rules.begin(), rules.end());
        break;

    case FWTOK_MATCH_STRICT_ALL:
        rules_strict_and.insert(rules_strict_and.end(), rules.begin(), rules.end());
        break;

    default:
        ss_dassert(false);
        break;
    }
}

static bool should_match(GWBUF* buffer)
{
    return modutil_is_SQL(buffer) || modutil_is_SQL_prepare(buffer) ||
           MYSQL_IS_COM_INIT_DB(GWBUF_DATA(buffer));
}

/**
 * Check if the query matches any of the rules in the user's rules.
 * @param my_instance Fwfilter instance
 * @param my_session Fwfilter session
 * @param queue The GWBUF containing the query
 * @param user The user whose rules are checked
 * @return True if the query matches at least one of the rules otherwise false
 */
bool User::match_any(FW_INSTANCE* my_instance, FW_SESSION* my_session,
                     GWBUF *queue, char** rulename)
{

    bool rval = false;

    if (rules_or.size() > 0 && should_match(queue))
    {
        char *fullquery = modutil_get_SQL(queue);

        if (fullquery)
        {
            for (RuleList::iterator it = rules_or.begin(); it != rules_or.end(); it++)
            {
                if (rule_is_active(*it))
                {
                    if (rule_matches(my_instance, my_session, queue, *it, fullquery))
                    {
                        *rulename = MXS_STRDUP_A((*it)->name().c_str());
                        rval = true;
                        break;
                    }
                }
            }

            MXS_FREE(fullquery);
        }
    }
    return rval;
}

/**
 * Check if the query matches all rules in the user's rules
 *
 * @param my_instance Filter instance
 * @param my_session  Filter session
 * @param queue       Buffer containing the query
 * @param user        The user whose rules are checked
 * @param strict_all  Whether the first match stops the processing
 * @param rulename    Pointer where error messages are stored
 *
 * @return True if the query matches all of the rules otherwise false
 */
bool User::do_match(FW_INSTANCE* my_instance, FW_SESSION* my_session,
                    GWBUF *queue, match_mode mode, char** rulename)
{
    bool rval = false;
    bool have_active_rule = false;
    std::string matching_rules;
    RuleList& rules = mode == User::ALL ? rules_and : rules_strict_and;

    if (rules.size() > 0 && should_match(queue))
    {
        char *fullquery = modutil_get_SQL(queue);

        if (fullquery)
        {
            rval = true;
            for (RuleList::iterator it = rules.begin(); it != rules.end(); it++)
            {
                if (!rule_is_active(*it))
                {
                    have_active_rule = true;

                    if (rule_matches(my_instance, my_session, queue, *it, fullquery))
                    {
                        matching_rules += (*it)->name();
                        matching_rules += " ";
                    }
                    else
                    {
                        rval = false;

                        if (mode == User::STRICT)
                        {
                            break;
                        }
                    }
                }
            }

            if (!have_active_rule)
            {
                /** No active rules */
                rval = false;
            }
            MXS_FREE(fullquery);
        }
    }

    /** Set the list of matched rule names */
    if (matching_rules.length() > 0)
    {
        *rulename = MXS_STRDUP_A(matching_rules.c_str());
    }

    return rval;
}

bool User::match(FW_INSTANCE* instance, FW_SESSION* session, GWBUF* buffer, char** rulename)
{
    return match_any(instance, session, buffer, rulename) ||
           do_match(instance, session, buffer, User::ALL, rulename) ||
           do_match(instance, session, buffer, User::STRICT, rulename);
}
