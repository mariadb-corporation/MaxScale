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

#include "cache_storage_api.h"


size_t cache_key_hash(const CACHE_KEY* key)
{
    ss_dassert(key);

    size_t hash = 0;

    const char* i   = key->data;
    const char* end = i + CACHE_KEY_MAXLEN;

    while (i < end)
    {
        int c = *i;
        hash = c + (hash << 6) + (hash << 16) - hash;
        ++i;
    }

    return hash;
}

bool cache_key_equal_to(const CACHE_KEY* lhs, const CACHE_KEY* rhs)
{
    ss_dassert(lhs);
    ss_dassert(rhs);

    return memcmp(lhs->data, rhs->data, CACHE_KEY_MAXLEN) == 0;
}


