/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <string>
#include <maxscale/ccdefs.hh>
#include <maxscale/filter.hh>
#include "binlogfiltersession.hh"

// Binlog Filter configuration
struct BinlogConfig
{
    BinlogConfig(const MXS_CONFIG_PARAMETER* pParams)
        : match(config_get_compiled_regex(pParams, "match", 0, nullptr))
        , md_match(match ? pcre2_match_data_create_from_pattern(match, nullptr) : nullptr)
        , exclude(config_get_compiled_regex(pParams, "exclude", 0, nullptr))
        , md_exclude(exclude ? pcre2_match_data_create_from_pattern(exclude, nullptr) : nullptr)
    {
    }

    pcre2_code*       match;
    pcre2_match_data* md_match;
    pcre2_code*       exclude;
    pcre2_match_data* md_exclude;
};

class BinlogFilter : public maxscale::Filter<BinlogFilter, BinlogFilterSession>
{
    // Prevent copy-constructor and assignment operator usage
    BinlogFilter(const BinlogFilter&);
    BinlogFilter& operator=(const BinlogFilter&);

public:
    ~BinlogFilter();

    // Creates a new filter instance
    static BinlogFilter* create(const char* zName,
                                MXS_CONFIG_PARAMETER* ppParams);

    // Creates a new session for this filter
    BinlogFilterSession* newSession(MXS_SESSION* pSession);

    // Print diagnostics to a DCB
    void diagnostics(DCB* pDcb) const;

    // Returns JSON form diagnostic data
    json_t* diagnostics_json() const;

    // Get filter capabilities
    uint64_t getCapabilities();

    // Return reference to filter config
    const BinlogConfig& getConfig() const
    {
        return m_config;
    }

private:
    // Constructor: used in the create function
    BinlogFilter(const MXS_CONFIG_PARAMETER* pParams);

private:
    BinlogConfig m_config;
};
