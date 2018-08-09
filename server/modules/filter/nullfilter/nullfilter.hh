#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>
#include <maxscale/filter.hh>
#include "nullfiltersession.hh"

class NullFilter : public maxscale::Filter<NullFilter, NullFilterSession>
{
public:
    ~NullFilter();
    static NullFilter* create(const char* zName, MXS_CONFIG_PARAMETER* pParams);

    NullFilterSession* newSession(MXS_SESSION* pSession);

    void diagnostics(DCB* pDcb);
    json_t* diagnostics_json() const;

    uint64_t getCapabilities();

private:
    NullFilter(const char* zName, uint64_t m_capabilities);

    NullFilter(const NullFilter&);
    NullFilter& operator = (const NullFilter&);

private:
    uint64_t m_capabilities;
};
