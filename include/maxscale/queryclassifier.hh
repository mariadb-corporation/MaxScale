#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <maxscale/router.h>
#include <maxscale/session.h>

namespace maxscale
{

class QueryClassifier
{
    QueryClassifier(const QueryClassifier&) = delete;
    QueryClassifier& operator = (const QueryClassifier&) = delete;

public:
    // NOTE: For the time being these must be exactly like the ones in readwritesplit.hh
    enum
    {
        TARGET_UNDEFINED = 0x00,
        TARGET_MASTER    = 0x01,
        TARGET_SLAVE     = 0x02,
        TARGET_ALL       = 0x08
    };

    QueryClassifier(MXS_SESSION* pSession,
                    mxs_target_t use_sql_variables_in);

    void set_load_active(bool active);
    bool load_active() const;

    uint32_t get_route_target(uint8_t command, uint32_t qtype);

private:
    MXS_SESSION* m_pSession;
    mxs_target_t m_use_sql_variables_in;
    bool         m_load_active;
};

}
