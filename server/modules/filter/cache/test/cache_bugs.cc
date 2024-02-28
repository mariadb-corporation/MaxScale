/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
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
#include <maxscale/paths.hh>
#include <maxscale/modinfo.hh>
#include "../cachemt.hh"

using namespace std;

namespace
{

int mxs_2727()
{
    int rv = 0;

    const size_t MAX_SIZE = 10;

    CacheConfig config("MXS-2727", nullptr);
    config.storage = std::string("storage_inmemory");
    config.soft_ttl = std::chrono::seconds(1);
    config.hard_ttl = std::chrono::seconds(10);
    config.max_size = MAX_SIZE;
    config.thread_model = CACHE_THREAD_MODEL_MT;
    config.enabled = true;

    mxs::set_libdir("../storage/storage_inmemory");
    CacheRules::SVector sRules(new CacheRules::Vector);
    sRules->push_back(shared_ptr<CacheRules>(CacheRules::create(&config)));
    Cache* pCache = CacheMT::create("MXS-2727", sRules, &config);
    mxb_assert(pCache);

    shared_ptr<Cache::Token> sToken;
    MXB_AT_DEBUG(bool created = ) pCache->create_token(&sToken);
    mxb_assert(created);

    CacheKey key;
    GWBUF select = mariadb::create_query("SELECT * FROM t");

    cache_result_t result = pCache->get_key(string(), string(), "test", select, &key);

    if (!CACHE_RESULT_IS_OK(result))
    {
        return 1;
    }

    vector<uint8_t> value(MAX_SIZE - 1);    // Less than max size.
    std::generate(value.begin(), value.end(), random);

    GWBUF buffer(value.data(), value.size());

    vector<string> invalidation_words;
    result = pCache->put_value(sToken.get(), key, invalidation_words, buffer);

    if (!CACHE_RESULT_IS_OK(result))
    {
        return 1;
    }

    value.push_back(4);
    value.push_back(2);
    // Now, larger than max size.

    buffer = GWBUF(value.data(), value.size());

    // This will crash without the MXS-2727 fix.
    result = pCache->put_value(sToken.get(), key, invalidation_words, buffer);

    // Expected to fail, as the value does not fit into the cache.
    if (CACHE_RESULT_IS_OK(result))
    {
        return 1;
    }

    delete pCache;

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
