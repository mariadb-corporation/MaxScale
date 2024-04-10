/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "producer.hh"

#include <maxbase/assert.hh>
#include <maxscale/service.hh>
#include <maxscale/mainworker.hh>
#include <maxsql/mariadb.hh>

namespace kafkaimporter
{

Producer::Producer(const Config& config, SERVICE* service)
    : m_config(config)
    , m_service(service)
{
}

Producer::Producer(Producer&& rhs) noexcept
    : m_config(rhs.m_config)
    , m_service(rhs.m_service)
    , m_mysql(rhs.m_mysql)
    , m_tables(std::move(rhs.m_tables))
{
    rhs.m_mysql = nullptr;
}

Producer& Producer::operator=(Producer&& rhs) noexcept
{
    mxb_assert(&m_config == &rhs.m_config);

    if (&rhs != this)
    {
        m_tables = std::move(rhs.m_tables);
        m_service = std::move(rhs.m_service);

        mysql_close(m_mysql);
        m_mysql = rhs.m_mysql;
        rhs.m_mysql = nullptr;
    }

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

Producer::ConnectionInfo Producer::find_master() const
{
    Producer::ConnectionInfo rval;

    mxs::MainWorker::get()->call(
        [this, &rval]() {
        SERVER* best = nullptr;
        rval.user = m_service->config()->user;
        rval.password = m_service->config()->password;

        for (SERVER* s : m_service->reachable_servers())
        {
            if (s->is_master() && (!best || s->rank() < best->rank()))
            {
                best = s;
            }
        }

        if (best)
        {
            rval.ok = true;
            rval.name = best->name();
            rval.host = best->address();
            rval.port = best->port();
            rval.proxy_protocol = best->proxy_protocol();
        }
    });

    return rval;
}

bool Producer::connect()
{
    bool ok = false;

    if (is_connected())
    {
        ok = true;
    }
    else
    {
        if (auto master = find_master())
        {
            int timeout = m_config.timeout.get().count();
            m_mysql = mysql_init(nullptr);

            mysql_optionsv(m_mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
            mysql_optionsv(m_mysql, MYSQL_OPT_READ_TIMEOUT, &timeout);
            mysql_optionsv(m_mysql, MYSQL_OPT_WRITE_TIMEOUT, &timeout);

            if (master.proxy_protocol)
            {
                mxq::set_proxy_header(m_mysql);
            }

            if (!mysql_real_connect(m_mysql, master.host.c_str(), master.user.c_str(),
                                    master.password.c_str(),
                                    nullptr, master.port, nullptr, 0))
            {
                MXB_ERROR("Failed to connect to '%s': %s", master.name.c_str(), mysql_error(m_mysql));
                mysql_close(m_mysql);
                m_mysql = nullptr;
            }
            else
            {
                MXB_INFO("Connected to '%s'", master.name.c_str());
                ok = true;
            }
        }
        else
        {
            MXB_ERROR("Could not find a valid Primary server to stream data into.");
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

        if (t.prepare(m_mysql, m_config.engine.get()))
        {
            MXB_INFO("Opened table '%s'", table.c_str());
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
