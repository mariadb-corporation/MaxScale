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
class BinlogConfig
{
public:
    // Constructor
    BinlogConfig(const MXS_CONFIG_PARAMETER* pParams)
        : active(config_get_bool(pParams, "filter_events"))
        , dbname(config_get_string(pParams, "skip_db"))
        , table(config_get_string(pParams, "skip_table"))
    {
    }

    // Destructor
    ~BinlogConfig()
    {
    }

    // Members mapped to config options
    bool        active;
    std::string dbname;
    std::string table;
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

    // Filter is active
    bool is_active() const;

    // Return reference to filter config
    const BinlogConfig& getConfig() const
    {
        return m_config;
    }

private:
    // Constructor: used in the create function
    BinlogFilter(const MXS_CONFIG_PARAMETER* pParams);

private:
    /**
     * Current configuration in maxscale.cnf
     *
     * [BinlogFilter]
     * type=filter
     * module=binlogfilter
     * filter_events=On
     * skip_table=t4
     * skip_db=test
     *
     * Note: Only one table and one db right now
     */
    BinlogConfig m_config;
};
