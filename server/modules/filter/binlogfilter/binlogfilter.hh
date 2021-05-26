/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <string>
#include <maxscale/ccdefs.hh>
#include <maxscale/config2.hh>
#include <maxscale/filter.hh>
#include <maxscale/pcre2.hh>
#include "binlogfiltersession.hh"

static constexpr const char REWRITE_SRC[] = "rewrite_src";
static constexpr const char REWRITE_DEST[] = "rewrite_dest";

// Binlog Filter configuration
struct BinlogConfig : public mxs::config::Configuration
{
    BinlogConfig(const char* name);

    mxs::config::RegexValue match;
    mxs::config::RegexValue exclude;
    mxs::config::RegexValue rewrite_src;
    std::string             rewrite_dest;
};

class BinlogFilter : public mxs::Filter
{
    // Prevent copy-constructor and assignment operator usage
    BinlogFilter(const BinlogFilter&);
    BinlogFilter& operator=(const BinlogFilter&);

public:
    ~BinlogFilter();

    // Creates a new filter instance
    static BinlogFilter* create(const char* zName);

    // Creates a new session for this filter
    BinlogFilterSession* newSession(MXS_SESSION* pSession, SERVICE* pService);

    // Returns JSON form diagnostic data
    json_t* diagnostics() const;

    // Get filter capabilities
    uint64_t getCapabilities() const;

    mxs::config::Configuration& getConfiguration()
    {
        return m_config;
    }

    // Return reference to filter config
    const BinlogConfig& getConfig() const
    {
        return m_config;
    }

private:
    // Constructor: used in the create function
    BinlogFilter(const char* name);

private:
    BinlogConfig m_config;
};
