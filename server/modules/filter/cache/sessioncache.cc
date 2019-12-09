/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "sessioncache.hh"

//static
std::unique_ptr<SessionCache> SessionCache::create(Cache* pCache)
{
    SessionCache* pThis = nullptr;

    std::unique_ptr<Cache::Token> sToken = pCache->create_token();

    if (sToken)
    {
        pThis = new (std::nothrow) SessionCache(pCache, std::move(sToken));
    }

    return std::unique_ptr<SessionCache>(pThis);
}
