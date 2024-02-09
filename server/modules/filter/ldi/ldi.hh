/*
 * Copyright (c) 2023 MariaDB plc
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
#include <maxscale/config2.hh>
#include <maxscale/filter.hh>
#include <maxscale/workerlocal.hh>
#include <maxscale/protocol/mariadb/module_names.hh>

class LDISession;

class LDI : public mxs::Filter
{
public:
    LDI(const LDI&) = delete;
    LDI& operator=(const LDI&) = delete;

    class Config : public mxs::config::Configuration
    {
    public:
        Config(const std::string& name);

        struct Values
        {
            std::string key;
            std::string secret;
            std::string region;
            std::string host;
            int64_t     port;
            int64_t     protocol_version;
            bool        no_verify;
            bool        use_http;
            std::string import_user;
            std::string import_password;
        };

        const Values& values()const
        {
            return *m_values;
        }

    private:
        bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override;

        Values                    m_v;
        mxs::WorkerGlobal<Values> m_values;
    };

    static LDI* create(const char* zName);

    mxs::FilterSession* newSession(MXS_SESSION* pSession, SERVICE* pService) override;

    json_t* diagnostics() const override;

    uint64_t getCapabilities() const override;

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

    std::set<std::string> protocols() const override
    {
        return {MXS_MARIADB_PROTOCOL_NAME};
    }

private:
    friend class LDISession;
    LDI(const std::string& name);

private:
    Config m_config;
};
