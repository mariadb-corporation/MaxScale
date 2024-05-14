/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file schemarouter.hh - Common schemarouter definitions
 */

#define MXB_MODULE_NAME "schemarouter"

#include <maxscale/ccdefs.hh>

#include <limits>
#include <list>
#include <set>
#include <string>
#include <memory>

#include <maxscale/buffer.hh>
#include <maxscale/pcre2.hh>
#include <maxscale/service.hh>
#include <maxscale/backend.hh>
#include <maxscale/protocol/mariadb/rwbackend.hh>
#include <maxscale/config2.hh>

namespace schemarouter
{
/**
 * Configuration values
 */
struct Config : public mxs::config::Configuration
{
    Config(const char* name);

    struct Values
    {
        std::chrono::seconds     refresh_interval;
        std::chrono::seconds     max_staleness;
        bool                     refresh_databases;
        bool                     debug;
        std::vector<std::string> ignore_tables;
        mxs::config::RegexValue  ignore_tables_regex;
    };

    const mxs::WorkerGlobal<Values>& values() const
    {
        return m_values;
    }

private:
    bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override
    {
        m_values.assign(m_v);
        return true;
    }

    Values                    m_v;
    mxs::WorkerGlobal<Values> m_values;
};

/**
 * Reference to a backend
 *
 * Owned by router client session.
 */
class SRBackend : public mxs::RWBackend
{
public:

    SRBackend(mxs::Endpoint* ref)
        : mxs::RWBackend(ref)
        , m_mapped(false)
    {
    }

    /**
     * @brief Set the mapping state of the backend
     *
     * @param value Value to set
     */
    void set_mapped(bool value)
    {
        m_mapped = value;
    }

    /**
     * @brief Check if the backend has been mapped
     *
     * @return True if the backend has been mapped
     */
    bool is_mapped() const
    {
        return m_mapped;
    }

private:
    bool m_mapped;      /**< Whether the backend has been mapped */
};

using SRBackendList = std::vector<std::unique_ptr<SRBackend>>;
}
