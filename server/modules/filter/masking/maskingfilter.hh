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
#include <memory>
#include <memory>
#include <maxscale/filter.hh>
#include "maskingfilterconfig.hh"
#include "maskingfiltersession.hh"

class MaskingRules;


class MaskingFilter : public mxs::Filter
{
public:
    typedef std::shared_ptr<MaskingRules> SMaskingRules;
    typedef MaskingFilterConfig           Config;

    ~MaskingFilter();
    static MaskingFilter* create(const char* zName, mxs::ConfigParameters* ppParams);

    MaskingFilterSession* newSession(MXS_SESSION* pSession, SERVICE* pService);

    json_t* diagnostics() const;

    uint64_t getCapabilities() const;

    bool reload();

    mxs::config::Configuration& getConfiguration()
    {
        return m_config;
    }

    const Config& config() const
    {
        return m_config;
    }
    SMaskingRules rules() const;

    bool post_configure();

private:
    MaskingFilter(const char* zName);

    MaskingFilter(const MaskingFilter&);
    MaskingFilter& operator=(const MaskingFilter&);

private:
    Config        m_config;
    SMaskingRules m_sRules;
};
