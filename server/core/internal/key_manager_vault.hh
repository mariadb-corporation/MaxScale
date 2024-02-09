/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/key_manager.hh>
#include <maxscale/config2.hh>

class VaultKey : public mxs::KeyManager::MasterKey
{
public:
    static mxs::config::Specification*                 specification();
    static std::unique_ptr<mxs::KeyManager::MasterKey> create(const mxs::ConfigParameters& options);

    std::tuple<bool, uint32_t, std::vector<uint8_t>>
    get_key(const std::string& id, uint32_t version) const override final;

    class Config : public mxs::config::Configuration
    {
    public:
        Config();

        std::string          token;
        std::string          host;
        int64_t              port;
        std::string          ca;
        std::string          mount;
        bool                 tls;
        std::chrono::seconds timeout;
    };

    VaultKey(Config config);

private:
    Config m_config;
};
