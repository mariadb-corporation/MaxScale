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
#include "examplefiltersession.hh"

class ExampleFilter : public maxscale::Filter<ExampleFilter, ExampleFilterSession>
{
    // Prevent copy-constructor and assignment operator usage
    ExampleFilter(const ExampleFilter&);
    ExampleFilter& operator=(const ExampleFilter&);

public:
    ~ExampleFilter();

    // Creates a new filter instance
    static ExampleFilter* create(const char* zName, MXS_CONFIG_PARAMETER* ppParams);

    // Creates a new session for this filter
    ExampleFilterSession* newSession(MXS_SESSION* pSession);

    // Print diagnostics to a DCB
    void diagnostics(DCB* pDcb) const;

    // Returns JSON form diagnostic data
    json_t* diagnostics_json() const;

    // Get filter capabilities
    uint64_t getCapabilities();

private:
    // Used in the create function
    ExampleFilter();
};
