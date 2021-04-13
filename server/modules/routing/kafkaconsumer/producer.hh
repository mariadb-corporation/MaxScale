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

#include <maxscale/ccdefs.hh>
#include <unordered_map>

#include "config.hh"
#include "table.hh"

namespace kafkaconsumer
{
class Producer
{
public:
    Producer& operator=(const Producer&) = delete;
    Producer(const Producer&) = delete;

    Producer& operator=(Producer&&);
    Producer(Producer&&);

    Producer(const Config&, SERVICE* service);
    ~Producer();

    bool produce(const std::string& table, const std::string& value);
    bool flush();

private:
    SERVER* find_master();
    bool    is_connected() const;
    bool    connect();

    const Config& m_config;
    SERVICE*      m_service;
    MYSQL*        m_mysql {nullptr};
    std::string   m_user;
    std::string   m_password;

    std::unordered_map<std::string, Table> m_tables;
};
}
