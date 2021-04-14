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
#pragma once

#include "config.hh"

#include <maxscale/ccdefs.hh>
#include <unordered_map>

#include "table.hh"

namespace kafkaimporter
{
class Producer final
{
public:
    Producer& operator=(const Producer&) = delete;
    Producer(const Producer&) = delete;

    Producer& operator=(Producer&&) noexcept;
    Producer(Producer&&) noexcept;

    Producer(const Config&, SERVICE* service);
    ~Producer();

    bool produce(const std::string& table, const std::string& value);
    bool flush();

private:
    struct ConnectionInfo
    {
        explicit operator bool() const
        {
            return ok;
        }

        bool        ok = false;
        std::string user;
        std::string password;
        std::string name;
        std::string host;
        int         port;
    };

    ConnectionInfo find_master() const;
    bool           is_connected() const;
    bool           connect();

    const Config& m_config;
    SERVICE*      m_service;
    MYSQL*        m_mysql {nullptr};

    std::unordered_map<std::string, Table> m_tables;
};
}
