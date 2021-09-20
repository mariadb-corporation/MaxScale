/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "xpandmon.hh"
#include <iostream>
#include <sstream>
#include <string>
#include "xpand.hh"

class XpandMembership
{
public:
    XpandMembership(int id,
                    xpand::Status status,
                    xpand::SubState substate,
                    int instance)
        : m_id(id)
        , m_status(status)
        , m_substate(substate)
        , m_instance(instance)
    {
    }

    int id() const
    {
        return m_id;
    }

    xpand::Status status() const
    {
        return m_status;
    }

    xpand::SubState substate() const
    {
        return m_substate;
    }

    int instance() const
    {
        return m_instance;
    }

    std::string to_string() const
    {
        std::stringstream ss;
        ss << "{"
           << m_id << ", "
           << xpand::to_string(m_status) << ", "
           << xpand::to_string(m_substate) << ", "
           << m_instance
           << "}";
        return ss.str();
    }

    void print(std::ostream& o) const
    {
        o << to_string();
    }

private:
    int             m_id;
    xpand::Status   m_status;
    xpand::SubState m_substate;
    int             m_instance;
};

inline std::ostream& operator<<(std::ostream& out, const XpandMembership& x)
{
    x.print(out);
    return out;
}
