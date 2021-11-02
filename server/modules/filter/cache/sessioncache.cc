/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "sessioncache.hh"

// static
std::unique_ptr<SessionCache> SessionCache::create(Cache* pCache)
{
    std::unique_ptr<SessionCache> sSession_cache;

    std::shared_ptr<Cache::Token> sToken;
    bool rv = pCache->create_token(&sToken);

    if (rv)
    {
        sSession_cache.reset(new(std::nothrow) SessionCache(pCache, std::move(sToken)));
    }
    else
    {
        MXS_ERROR("Cache storage token creation failed.");
    }

    return sSession_cache;
}
