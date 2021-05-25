/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include "cache.hh"

class SessionCache
{
public:
    SessionCache(const SessionCache&) = delete;
    SessionCache& operator=(const SessionCache&) = delete;

    /**
     * Create session specific cache instance. Note that "session specific" _only_
     * means that the communication between the session and the cache does not
     * affect other sessions, not that the cached data would be unique for the
     * session.
     *
     * @return A new instance or nullptr if a cache token cannot be created.
     */
    static std::unique_ptr<SessionCache> create(Cache* pCache);

    /**
     * @see Cache::config
     */
    const CacheConfig& config() const
    {
        return m_cache.config();
    }

    /**
     * @see Cache::should_store
     */
    const CacheRules* should_store(const char* zDefaultDb, const GWBUF* pQuery)
    {
        return m_cache.should_store(zDefaultDb, pQuery);
    }

    /**
     * @see Cache::must_refresh
     */
    bool must_refresh(const CacheKey& key, const CacheFilterSession* pSession)
    {
        return m_cache.must_refresh(key, pSession);
    }

    /**
     * @see Cache::refreshed
     */
    void refreshed(const CacheKey& key, const CacheFilterSession* pSession)
    {
        return m_cache.refreshed(key, pSession);
    }

    /**
     * @see Cache::get_key
     */
    cache_result_t get_key(const std::string& user,
                           const std::string& host,
                           const char* zDefault_db,
                           const GWBUF* pQuery,
                           CacheKey* pKey) const
    {
        return m_cache.get_key(user, host, zDefault_db, pQuery, pKey);
    }

    /**
     * @See Cache::get_value
     */
    cache_result_t get_value(const CacheKey& key,
                             uint32_t flags,
                             uint32_t soft_ttl,
                             uint32_t hard_ttl,
                             GWBUF** ppValue,
                             const std::function<void (cache_result_t, GWBUF*)>& cb) const
    {
        return m_cache.get_value(token(), key, flags, soft_ttl, hard_ttl, ppValue, cb);
    }

    /**
     * @see Cache::put_value
     */
    cache_result_t put_value(const CacheKey& key,
                             const std::vector<std::string>& invalidation_words,
                             const GWBUF* pValue,
                             const std::function<void (cache_result_t)>& cb)
    {
        return m_cache.put_value(token(), key, invalidation_words, pValue, cb);
    }

    /**
     * @see Cache::del_value
     */
    cache_result_t del_value(const CacheKey& key,
                             const std::function<void (cache_result_t)>& cb)
    {
        return m_cache.del_value(token(), key, cb);
    }

    /**
     * @see Cache::invalidate
     */
    cache_result_t invalidate(const std::vector<std::string>& words,
                              const std::function<void (cache_result_t)>& cb)
    {
        return m_cache.invalidate(token(), words, cb);
    }

    /**
     * @see Cache::clear
     */
    cache_result_t clear()
    {
        return m_cache.clear(token());
    }

protected:
    SessionCache(Cache* pCache,
                 std::shared_ptr<Cache::Token> sToken)
        : m_cache(*pCache)
        , m_sToken(std::move(sToken))
    {
    }

private:
    Cache::Token* token() const
    {
        return m_sToken.get();
    }

private:
    Cache&                        m_cache;
    std::shared_ptr<Cache::Token> m_sToken;
};
