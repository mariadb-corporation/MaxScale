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

#include <maxscale/ccdefs.hh>
#include <maxscale/filter.hh>
#include "commentfiltersession.hh"
#include <string>

class CommentFilter : public maxscale::Filter<CommentFilter, CommentFilterSession>
{
public:
    // Prevent copy-constructor and assignment operator usage
    CommentFilter(const CommentFilter&) = delete;
    CommentFilter& operator=(const CommentFilter&) = delete;

    ~CommentFilter();

    // Creates a new filter instance
    static CommentFilter* create(const char* zName, MXS_CONFIG_PARAMETER* ppParams);

    // Creates a new session for this filter
    CommentFilterSession* newSession(MXS_SESSION* pSession);

    // Print diagnostics to a DCB
    void diagnostics(DCB* pDcb) const;


    // Returns JSON form diagnostic data
    json_t* diagnostics_json() const;

    // Get filter capabilities
    uint64_t getCapabilities();
    std::string comment() const
    {
        return m_comment;
    }

private:
    std::string m_comment;
    // Used in the create function
    CommentFilter(std::string comment);
};
