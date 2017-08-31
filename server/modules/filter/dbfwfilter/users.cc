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

#include "users.hh"

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
