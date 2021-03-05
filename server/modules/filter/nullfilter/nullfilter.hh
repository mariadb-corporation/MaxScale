/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/config2.hh>
#include <maxscale/filter.hh>
#include "nullfiltersession.hh"

class NullFilter : public mxs::Filter
{
public:
    NullFilter(const NullFilter&) = delete;
    NullFilter& operator=(const NullFilter&) = delete;

    class Config : public mxs::config::Configuration
    {
    public:
        Config(const std::string& name);

        uint32_t capabilities;
    };

    static NullFilter* create(const char* zName);

    NullFilterSession* newSession(MXS_SESSION* pSession, SERVICE* pService);

    json_t* diagnostics() const;

    uint64_t getCapabilities() const;

    mxs::config::Configuration& getConfiguration()
    {
        return m_config;
    }

private:
    NullFilter(const std::string& name);

private:
    Config m_config;
};
