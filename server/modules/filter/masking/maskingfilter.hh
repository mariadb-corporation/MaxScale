#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <maxscale/filter.hh>
#include "maskingfilterconfig.hh"
#include "maskingfiltersession.hh"


class MaskingFilter : public maxscale::Filter<MaskingFilter, MaskingFilterSession>
{
public:
    typedef MaskingFilterConfig Config;

    ~MaskingFilter();
    static MaskingFilter* create(const char* zName, char** pzOptions, CONFIG_PARAMETER* ppParams);

    MaskingFilterSession* newSession(SESSION* pSession);

    void diagnostics(DCB* pDcb);

    static uint64_t getCapabilities();

private:
    MaskingFilter();

    MaskingFilter(const MaskingFilter&);
    MaskingFilter& operator = (const MaskingFilter&);

    static bool process_params(char **pzOptions, CONFIG_PARAMETER *ppParams, Config& config);

private:
    Config m_config;
};
