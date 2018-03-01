#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <memory>
#include <tr1/memory>
#include <maxscale/filter.hh>
#include "maskingfilterconfig.hh"
#include "maskingfiltersession.hh"

class MaskingRules;


class MaskingFilter : public maxscale::Filter<MaskingFilter, MaskingFilterSession>
{
public:
    typedef std::tr1::shared_ptr<MaskingRules> SMaskingRules;
    typedef MaskingFilterConfig Config;

    ~MaskingFilter();
    static MaskingFilter* create(const char* zName, char** pzOptions, MXS_CONFIG_PARAMETER* ppParams);

    MaskingFilterSession* newSession(MXS_SESSION* pSession);

    void diagnostics(DCB* pDcb);

    uint64_t getCapabilities();

    void reload(DCB* pOut);

    const Config& config() const
    {
        return m_config;
    }
    SMaskingRules rules() const;

private:
    MaskingFilter(const Config& config, std::auto_ptr<MaskingRules> sRules);

    MaskingFilter(const MaskingFilter&);
    MaskingFilter& operator = (const MaskingFilter&);

private:
    Config        m_config;
    SMaskingRules m_sRules;
};
