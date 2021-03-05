/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/filter.hh>
#include "commentfiltersession.hh"
#include <string>
#include "commentconfig.hh"

class CommentFilter : public mxs::Filter
{
public:
    // Prevent copy-constructor and assignment operator usage
    CommentFilter(const CommentFilter&) = delete;
    CommentFilter& operator=(const CommentFilter&) = delete;

    ~CommentFilter();

    // Creates a new filter instance
    static CommentFilter* create(const char* zName, mxs::ConfigParameters* ppParams);

    // Creates a new session for this filter
    CommentFilterSession* newSession(MXS_SESSION* pSession, SERVICE* pService);

    // Returns JSON form diagnostic data
    json_t* diagnostics() const;

    // Get filter capabilities
    uint64_t getCapabilities() const;

    const CommentConfig& config() const
    {
        return m_config;
    }

    mxs::config::Configuration& getConfiguration()
    {
        return m_config;
    }

private:
    // Used in the create function
    CommentFilter(CommentConfig&& config);

    CommentConfig m_config;
};
