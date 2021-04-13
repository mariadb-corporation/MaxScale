/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "producer.hh"
#include "config.hh"

#include <maxbase/assert.h>
#include <maxscale/service.hh>

namespace kafkaconsumer
{

Producer::Producer(const Config& config, SERVICE* service)
    : m_config(config)
    , m_service(service)
    , m_user(service->config()->user)
    , m_password(service->config()->password)
{
}

Producer::Producer(Producer&& rhs)
    : m_config(rhs.m_config)
{
    *this = std::move(rhs);
}

Producer& Producer::operator=(Producer&& rhs)
{
    mxb_assert(&m_config == &rhs.m_config);
    m_user = std::move(rhs.m_user);
    m_password = std::move(rhs.m_password);
    m_tables = std::move(rhs.m_tables);
    m_service = std::move(rhs.m_service);

    mysql_close(m_mysql);
    m_mysql = rhs.m_mysql;
    rhs.m_mysql = nullptr;

    return *this;
}

Producer::~Producer()
{
    // Clear out m_tables to make sure the MYSQL_STMT is freed before the MYSQL handle is
    m_tables.clear();
    mysql_close(m_mysql);
}

bool Producer::is_connected() const
{
    return m_mysql != nullptr;
}

SERVER* Producer::find_master()
{
    SERVER* rval = nullptr;

    for (SERVER* s : m_service->reachable_servers())
    {
        if (s->is_master() && (!rval || s->rank() < rval->rank()))
        {
            rval = s;
        }
    }

    return rval;
}

bool Producer::connect()
{
    bool ok = true;

    if (!is_connected())
    {
        if (SERVER* best = find_master())
        {
            m_mysql = mysql_init(nullptr);

            if (!mysql_real_connect(m_mysql, best->address(), m_user.c_str(), m_password.c_str(),
                                    nullptr, best->port(), nullptr, 0))
            {
                ok = false;
                MXS_ERROR("Failed to connect to '%s': %s", best->name(), mysql_error(m_mysql));
                mysql_close(m_mysql);
                m_mysql = nullptr;
            }
        }
        else
        {
            MXS_ERROR("Could not find a valid Master server to stream data into.");
        }
    }

    return ok;
}

bool Producer::flush()
{
    mxb_assert(is_connected());
    bool ok = true;

    for (auto& kv : m_tables)
    {
        if (!kv.second.flush())
        {
            ok = false;
            break;
        }
    }

    return ok;
}

bool Producer::produce(const std::string& table, const std::string& value)
{
    if (!connect())
    {
        return false;
    }

    bool ok = false;
    auto it = m_tables.find(table);

    if (it == m_tables.end())
    {
        Table t(table);

        if (t.prepare(m_mysql))
        {
            MXS_INFO("Opened table '%s'", table.c_str());
            it = m_tables.emplace(table, std::move(t)).first;
        }
    }

    if (it != m_tables.end())
    {
        ok = it->second.insert(value);
    }

    return ok;
}
}
