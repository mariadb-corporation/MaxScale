/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include "clustrixmon.hh"
#include <iostream>
#include <sstream>
#include <string>
#include "clustrix.hh"

class ClustrixMembership
{
public:
    ClustrixMembership(int id,
                       Clustrix::Status status,
                       Clustrix::SubState substate,
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

    Clustrix::Status status() const
    {
        return m_status;
    }

    Clustrix::SubState substate() const
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
           << Clustrix::to_string(m_status) << ", "
           << Clustrix::to_string(m_substate) << ", "
           << m_instance
           << "}";
        return ss.str();
    }

    void print(std::ostream& o) const
    {
        o << to_string();
    }

private:
    int                m_id;
    Clustrix::Status   m_status;
    Clustrix::SubState m_substate;
    int                m_instance;
};

inline std::ostream& operator << (std::ostream& out, const ClustrixMembership& x)
{
    x.print(out);
    return out;
}
