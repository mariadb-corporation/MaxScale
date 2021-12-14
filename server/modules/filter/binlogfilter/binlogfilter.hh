/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <string>
#include <maxscale/ccdefs.hh>
#include <maxscale/filter.hh>
#include <maxscale/pcre2.hh>
#include "binlogfiltersession.hh"

static constexpr const char REWRITE_SRC[] = "rewrite_src";
static constexpr const char REWRITE_DEST[] = "rewrite_dest";

// Binlog Filter configuration
struct BinlogConfig
{
    BinlogConfig(const mxs::ConfigParameters* pParams)
        : match(pParams->get_compiled_regex("match", 0, nullptr).release())
        , md_match(match ? pcre2_match_data_create_from_pattern(match, nullptr) : nullptr)
        , exclude(pParams->get_compiled_regex("exclude", 0, nullptr).release())
        , md_exclude(exclude ? pcre2_match_data_create_from_pattern(exclude, nullptr) : nullptr)
        , rewrite_src(pParams->get_compiled_regex(REWRITE_SRC, 0, nullptr).release())
        , rewrite_src_pattern(pParams->get_string(REWRITE_SRC))
        , rewrite_dest(pParams->get_string(REWRITE_DEST))
    {
    }

    ~BinlogConfig()
    {
        pcre2_code_free(match);
        pcre2_match_data_free(md_match);
        pcre2_code_free(exclude);
        pcre2_match_data_free(md_exclude);
        pcre2_code_free(rewrite_src);
    }

    pcre2_code*       match;
    pcre2_match_data* md_match;
    pcre2_code*       exclude;
    pcre2_match_data* md_exclude;
    pcre2_code*       rewrite_src;
    std::string       rewrite_src_pattern;
    std::string       rewrite_dest;
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
                                mxs::ConfigParameters* ppParams);

    // Creates a new session for this filter
    BinlogFilterSession* newSession(MXS_SESSION* pSession, SERVICE* pService);

    // Returns JSON form diagnostic data
    json_t* diagnostics() const;

    // Get filter capabilities
    uint64_t getCapabilities();

    // Return reference to filter config
    const BinlogConfig& getConfig() const
    {
        return m_config;
    }

private:
    // Constructor: used in the create function
    BinlogFilter(const mxs::ConfigParameters* pParams);

private:
    BinlogConfig m_config;
};
