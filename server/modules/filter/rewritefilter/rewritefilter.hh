/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-06-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include "rewritesession.hh"
#include <maxscale/config2.hh>
#include <maxscale/filter.hh>

class RewriteFilterSession;

class RewriteFilter : public mxs::Filter
{
public:
    RewriteFilter(const RewriteFilter&) = delete;
    RewriteFilter& operator=(const RewriteFilter&) = delete;

    class Config : public mxs::config::Configuration
    {
    public:
        Config(const std::string& name);

        uint32_t capabilities;
    };

    static RewriteFilter* create(const char* zName);

    RewriteFilterSession* newSession(MXS_SESSION* pSession, SERVICE* pService) override;

    json_t* diagnostics() const override;

    uint64_t getCapabilities() const override;

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

private:
    RewriteFilter(const std::string& name);

private:
    Config m_config;
};
