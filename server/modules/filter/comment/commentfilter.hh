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

#include <maxscale/ccdefs.hh>
#include <maxscale/filter.hh>
#include <maxscale/protocol/mariadb/module_names.hh>
#include "commentfiltersession.hh"
#include <string>
#include "commentconfig.hh"

class CommentFilter : public mxs::Filter
{
public:
    // Prevent copy-constructor and assignment operator usage
    CommentFilter(const CommentFilter&) = delete;
    CommentFilter& operator=(const CommentFilter&) = delete;

    // Creates a new filter instance
    static CommentFilter* create(const char* zName);

    // Creates a new session for this filter
    std::shared_ptr<mxs::FilterSession> newSession(MXS_SESSION* pSession, SERVICE* pService) override;

    // Returns JSON form diagnostic data
    json_t* diagnostics() const override;

    // Get filter capabilities
    uint64_t getCapabilities() const override;

    const CommentConfig& config() const
    {
        return m_config;
    }

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

    std::set<std::string> protocols() const override
    {
        return {MXS_MARIADB_PROTOCOL_NAME};
    }

private:
    // Used in the create function
    CommentFilter(const std::string& name);

    CommentConfig m_config;
};
