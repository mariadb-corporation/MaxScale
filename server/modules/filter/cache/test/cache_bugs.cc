/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>
#include <iostream>
#include <string>
#include <vector>
#include <maxbase/log.hh>
#include <maxscale/paths.h>
#include <maxscale/modinfo.h>
#include "../cachemt.hh"

using namespace std;

namespace
{

GWBUF* create_gwbuf(const string& s)
{
    size_t len = s.length();
    size_t payload_len = len + 1;
    size_t gwbuf_len = MYSQL_HEADER_LEN + payload_len;

    GWBUF* pBuf = gwbuf_alloc(gwbuf_len);

    *((unsigned char*)((char*)GWBUF_DATA(pBuf))) = payload_len;
    *((unsigned char*)((char*)GWBUF_DATA(pBuf) + 1)) = (payload_len >> 8);
    *((unsigned char*)((char*)GWBUF_DATA(pBuf) + 2)) = (payload_len >> 16);
    *((unsigned char*)((char*)GWBUF_DATA(pBuf) + 3)) = 0x00;
    *((unsigned char*)((char*)GWBUF_DATA(pBuf) + 4)) = 0x03;
    memcpy((char*)GWBUF_DATA(pBuf) + 5, s.c_str(), len);

    return pBuf;
}

int mxs_2727()
{
    int rv = 0;

    const size_t MAX_SIZE = 10;

    CacheConfig config("MXS-2727");
    config.storage = std::string("storage_inmemory");
    config.soft_ttl = std::chrono::seconds(1);
    config.hard_ttl = std::chrono::seconds(10);
    config.max_size = MAX_SIZE;
    config.thread_model = CACHE_THREAD_MODEL_MT;
    config.enabled = true;

    set_libdir(const_cast<char*>("../storage/storage_inmemory"));
    auto* pCache = CacheMT::Create("MXS-2727", &config);
    mxb_assert(pCache);

    CACHE_KEY key;
    GWBUF* pSelect = create_gwbuf("SELECT * FROM t");

    cache_result_t result = pCache->get_key("test", pSelect, &key);

    if (!CACHE_RESULT_IS_OK(result))
    {
        return 1;
    }

    vector<uint8_t> value(MAX_SIZE - 1); // Less than max size.
    std::generate(value.begin(), value.end(), random);

    GWBUF* pValue = gwbuf_alloc_and_load(value.size(), &value.front());

    result = pCache->put_value(key, pValue);
    gwbuf_free(pValue);

    if (!CACHE_RESULT_IS_OK(result))
    {
        return 1;
    }

    value.push_back(4);
    value.push_back(2);
    // Now, larger than max size.

    pValue = gwbuf_alloc_and_load(value.size(), &value.front());

    // This will crash without the MXS-2727 fix.
    result = pCache->put_value(key, pValue);
    gwbuf_free(pValue);

    // Expected to fail, as the value does not fit into the cache.
    if (CACHE_RESULT_IS_OK(result))
    {
        return 1;
    }

    return 0;
}

}

int main()
{
    mxb::Log log;

    int rv = 0;

    rv += mxs_2727();

    return rv == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
