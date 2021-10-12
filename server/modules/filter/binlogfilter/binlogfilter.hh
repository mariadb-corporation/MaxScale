/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/filter.hh>

#include "binlogconfig.hh"
#include "binlogfiltersession.hh"

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
    BinlogFilterSession* newSession(MXS_SESSION* pSession, SERVICE* pService) override;

    // Returns JSON form diagnostic data
    json_t* diagnostics() const override;

    // Get filter capabilities
    uint64_t getCapabilities() const override;

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

    // Return reference to filter config
    const BinlogConfig::Values& getConfig() const
    {
        return m_config.values();
    }

private:
    // Constructor: used in the create function
    BinlogFilter(const char* name);

private:
    BinlogConfig m_config;
};
