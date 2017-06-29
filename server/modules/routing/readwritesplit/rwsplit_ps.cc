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

#include "readwritesplit.hh"

#include <maxscale/alloc.h>
#include <maxscale/query_classifier.h>

std::string extract_text_ps_id(GWBUF* buffer)
{
    std::string rval;
    char* name = qc_get_prepare_name(buffer);

    if (name)
    {
        rval = name;
        MXS_FREE(name);
    }

    return rval;
}

void store_text_ps(ROUTER_CLIENT_SES* rses, std::string id, GWBUF* buffer)
{
    GWBUF* stmt = qc_get_preparable_stmt(buffer);
    ss_dassert(stmt);

    uint32_t type = qc_get_type_mask(stmt);
    ss_dassert((type & (QUERY_TYPE_PREPARE_STMT | QUERY_TYPE_PREPARE_NAMED_STMT)) == 0);

    rses->ps_text[id] = type;
}

void erase_text_ps(ROUTER_CLIENT_SES* rses, std::string id)
{
    rses->ps_text.erase(id);
}

bool get_text_ps_type(ROUTER_CLIENT_SES* rses, GWBUF* buffer, uint32_t* out)
{
    bool rval = false;
    char* name = qc_get_prepare_name(buffer);

    if (name)
    {
        TextPSMap::iterator it = rses->ps_text.find(name);

        if (it != rses->ps_text.end())
        {
            *out = it->second;
            rval = true;
        }

        MXS_FREE(name);
    }

    return rval;
}
