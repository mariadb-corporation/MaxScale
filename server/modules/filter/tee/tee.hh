/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <string>
#include <regex.h>

#include <maxscale/filter.hh>
#include <maxscale/service.hh>
#include <maxscale/config2.hh>

#include "teesession.hh"

/**
 * The instance structure for the TEE filter - this holds the configuration
 * information for the filter.
 */
class Tee : public mxs::Filter
{
    Tee(const Tee&);
    const Tee& operator=(const Tee&);
public:

    class Config : public mxs::config::Configuration
    {
    public:
        struct Values
        {
            mxs::Target*            target;
            SERVICE*                service;
            std::string             user;   /* The user name to filter on */
            std::string             source; /* The source of the client connection */
            mxs::config::RegexValue match;  /* Compiled match pattern */
            mxs::config::RegexValue exclude;/* Compiled exclude pattern*/
            bool                    sync;   /* Wait for replies before routing more */
        };

        Config(const char* name);

        const Values& values() const
        {
            return *m_values;
        }

    private:
        bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override;

        Values                    m_v;
        mxs::WorkerGlobal<Values> m_values;
    };

    static Tee* create(const char* zName);
    TeeSession* newSession(MXS_SESSION* session, SERVICE* service) override;
    json_t*     diagnostics() const override;

    uint64_t getCapabilities() const override
    {
        return RCAP_TYPE_STMT_INPUT;
    }

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

    const Config::Values& config() const
    {
        return m_config.values();
    }

    void set_enabled(bool value)
    {
        m_enabled = value;
    }

    bool is_enabled() const
    {
        return m_enabled;
    }

private:
    Tee(const char* name);

    std::string m_name;
    Config      m_config;
    bool        m_enabled;
};
