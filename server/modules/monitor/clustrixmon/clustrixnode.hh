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
#include "clustrixmembership.hh"

class ClustrixNode
{
public:
    enum
    {
        DEFAULT_MYSQL_PORT  = 3306,
        DEFAULT_HEALTH_PORT = 3581,
    };

    enum approach_t
    {
        APPROACH_OVERRIDE,
        APPROACH_DEFAULT
    };

    ClustrixNode(const ClustrixMembership& membership,
                 const std::string& ip,
                 int mysql_port,
                 int health_port,
                 int health_check_threshold,
                 SERVER* pServer)
        : m_id(membership.id())
        , m_status(membership.status())
        , m_substate(membership.substate())
        , m_instance(membership.instance())
        , m_ip(ip)
        , m_mysql_port(mysql_port)
        , m_health_port(health_port)
        , m_health_check_threshold(health_check_threshold)
        , m_nRunning(m_health_check_threshold)
        , m_pServer(pServer)
        , m_pCon(nullptr)
    {
    }

    ~ClustrixNode()
    {
        if (m_pCon)
        {
            mysql_close(m_pCon);
        }
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

    const std::string& ip() const
    {
        return m_ip;
    }

    void set_ip(const std::string& ip)
    {
        m_ip = ip;
        m_pServer->server_update_address(ip);
    }

    int mysql_port() const
    {
        return m_mysql_port;
    }

    void set_mysql_port(int port)
    {
        m_mysql_port = port;
        m_pServer->update_port(port);
    }

    int health_port() const
    {
        return m_health_port;
    }

    void set_health_port(int port)
    {
        m_health_port = port;
    }

    bool is_running() const
    {
	return m_nRunning > 0;
    }

    void set_running(bool running, approach_t approach = APPROACH_DEFAULT)
    {
        if (running)
        {
            m_nRunning = m_health_check_threshold;

            m_pServer->set_status(SERVER_RUNNING);
        }
        else
        {
            if (m_nRunning > 0)
            {
                if (approach == APPROACH_OVERRIDE)
                {
                    m_nRunning = 0;
                }
                else
                {
                    --m_nRunning;
                }

                if (m_nRunning == 0)
                {
                    m_pServer->clear_status(SERVER_RUNNING);
                }
            }
        }
    }

    void update(Clustrix::Status status, Clustrix::SubState substate, int instance)
    {
        m_status = status;
        m_substate = substate;
        m_instance = instance;
    }

    void deactivate_server()
    {
        m_pServer->is_active = false;
    }

    bool can_be_used_as_hub(const MXS_MONITORED_SERVER::ConnectionSettings& sett);

    SERVER* server() const
    {
        return m_pServer;
    }

    MYSQL* connection() const
    {
        return m_pCon;
    }

    MYSQL* release_connection()
    {
        MYSQL* pCon = m_pCon;
        m_pCon = nullptr;
        return pCon;
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
    int                m_id;
    Clustrix::Status   m_status;
    Clustrix::SubState m_substate;
    int                m_instance;
    std::string        m_ip;
    int                m_mysql_port             { DEFAULT_MYSQL_PORT };
    int                m_health_port            { DEFAULT_HEALTH_PORT };
    int                m_health_check_threshold { DEFAULT_HEALTH_CHECK_THRESHOLD_VALUE };
    int                m_nRunning               { 0 };
    SERVER*            m_pServer                { nullptr };
    MYSQL*             m_pCon                   { nullptr };
};

inline std::ostream& operator << (std::ostream& out, const ClustrixNode& x)
{
    x.print(out);
    return out;
}
