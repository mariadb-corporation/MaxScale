/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
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

void User::add_rules(match_type mode, const RuleList& rules)
{
    switch (mode)
    {
    case FWTOK_MATCH_ANY:
        rules_or_vector.push_back(rules);
        break;

    case FWTOK_MATCH_ALL:
        rules_and_vector.push_back(rules);
        break;

    case FWTOK_MATCH_STRICT_ALL:
        rules_strict_and_vector.push_back(rules);
        break;

    default:
        mxb_assert(false);
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
bool User::match_any(Dbfw* my_instance, DbfwSession* my_session,
                     GWBUF *queue, char** rulename)
{

    bool rval = false;

    for (RuleListVector::iterator i = rules_or_vector.begin(); i != rules_or_vector.end(); ++i)
    {
        RuleList& rules_or = *i;

        if (rules_or.size() > 0 && should_match(queue))
        {
            char *fullquery = modutil_get_SQL(queue);

            if (fullquery)
            {
                for (RuleList::iterator j = rules_or.begin(); j != rules_or.end(); j++)
                {
                    if (rule_is_active(*j))
                    {
                        if (rule_matches(my_instance, my_session, queue, *j, fullquery))
                        {
                            *rulename = MXS_STRDUP_A((*j)->name().c_str());
                            rval = true;
                            break;
                        }
                    }
                }

                MXS_FREE(fullquery);
            }
        }

        if (rval)
        {
            break;
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
bool User::do_match(Dbfw* my_instance, DbfwSession* my_session,
                    GWBUF *queue, match_mode mode, char** rulename)
{
    bool rval = false;
    bool have_active_rule = false;
    std::string matching_rules;
    RuleListVector& rules_vector = (mode == User::ALL ? rules_and_vector : rules_strict_and_vector);

    for (RuleListVector::iterator i = rules_vector.begin(); i != rules_vector.end(); ++i)
    {
        RuleList& rules = *i;

        if (rules.size() > 0 && should_match(queue))
        {
            char *fullquery = modutil_get_SQL(queue);

            if (fullquery)
            {
                rval = true;
                for (RuleList::iterator j = rules.begin(); j != rules.end(); j++)
                {
                    if (rule_is_active(*j))
                    {
                        have_active_rule = true;

                        if (rule_matches(my_instance, my_session, queue, *j, fullquery))
                        {
                            matching_rules += (*j)->name();
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

        if (rval)
        {
            break;
        }
    }

    /** Set the list of matched rule names */
    if (matching_rules.length() > 0)
    {
        *rulename = MXS_STRDUP_A(matching_rules.c_str());
    }

    return rval;
}

bool User::match(Dbfw* instance, DbfwSession* session, GWBUF* buffer, char** rulename)
{
    return match_any(instance, session, buffer, rulename) ||
           do_match(instance, session, buffer, User::ALL, rulename) ||
           do_match(instance, session, buffer, User::STRICT, rulename);
}
