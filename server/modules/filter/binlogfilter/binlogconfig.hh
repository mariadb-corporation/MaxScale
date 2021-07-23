/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/config2.hh>
#include <maxscale/workerlocal.hh>

static constexpr const char REWRITE_SRC[] = "rewrite_src";
static constexpr const char REWRITE_DEST[] = "rewrite_dest";

// Binlog Filter configuration
class BinlogConfig : public mxs::config::Configuration
{
public:
    BinlogConfig(const char* name);

    struct Values
    {
        mxs::config::RegexValue match;
        mxs::config::RegexValue exclude;
        mxs::config::RegexValue rewrite_src;
        std::string             rewrite_dest;
    };

    const Values& values() const
    {
        return *m_values;
    }

    static mxs::config::Specification* specification();

    bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override
    {
        m_values.assign(m_v);
        return true;
    }

private:
    Values                    m_v;
    mxs::WorkerGlobal<Values> m_values;
};
