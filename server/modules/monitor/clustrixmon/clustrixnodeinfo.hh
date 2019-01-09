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

class ClustrixNodeInfo
{
public:
    ClustrixNodeInfo(int id,
                     const std::string& ip,
                     int mysql_port,
                     int health_port)
        : m_id(id)
        , m_ip(ip)
        , m_mysql_port(mysql_port)
        , m_health_port(health_port)
    {
    }

    int id() const
    {
        return m_id;
    }

    const std::string& ip() const
    {
        return m_ip;
    }

    int mysql_port() const
    {
        return m_mysql_port;
    }

    int health_port() const
    {
        return m_health_port;
    }

    bool is_running() const
    {
	return m_is_running;
    }

    void set_running(bool running)
    {
	m_is_running = running;
    }

    std::string to_string() const
    {
        std::stringstream ss;
        ss << "{" << m_id << ", " << m_ip << ", " << m_mysql_port << ", " << m_health_port << "}";
        return ss.str();
    }

    void print(std::ostream& o) const
    {
        o << to_string();
    }

private:
    int         m_id;
    std::string m_ip;
    int         m_mysql_port;
    int         m_health_port;
    bool        m_is_running   { true }; // Assume running, until proven otherwise.
};

inline std::ostream& operator << (std::ostream& out, const ClustrixNodeInfo& x)
{
    x.print(out);
    return out;
}
