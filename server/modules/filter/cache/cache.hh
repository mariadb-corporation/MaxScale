/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <maxscale/buffer.hh>
#include <maxscale/session.hh>

#include "rules.hh"
#include "cache_storage_api.hh"
#include "cacheconfig.hh"

class CacheFilterSession;
class StorageFactory;

#define CACHE_DEBUG_NONE         0  /* 0b00000 */
#define CACHE_DEBUG_MATCHING     1  /* 0b00001 */
#define CACHE_DEBUG_NON_MATCHING 2  /* 0b00010 */
#define CACHE_DEBUG_USE          4  /* 0b00100 */
#define CACHE_DEBUG_NON_USE      8  /* 0b01000 */
#define CACHE_DEBUG_DECISIONS    16 /* 0b10000 */

#define CACHE_DEBUG_RULES (CACHE_DEBUG_MATCHING | CACHE_DEBUG_NON_MATCHING)
#define CACHE_DEBUG_USAGE (CACHE_DEBUG_USE | CACHE_DEBUG_NON_USE)
#define CACHE_DEBUG_MIN   CACHE_DEBUG_NONE
#define CACHE_DEBUG_MAX   (CACHE_DEBUG_RULES | CACHE_DEBUG_USAGE | CACHE_DEBUG_DECISIONS)

#if !defined (UINT32_MAX)
#define UINT32_MAX (4294967295U)
#endif

#if !defined (UINT64_MAX)
#define UINT64_MAX (18446744073709551615UL)
#endif

class Cache
{
public:
    using Token = Storage::Token;

    enum what_info_t
    {
        INFO_RULES   = 0x01,/*< Include information about the rules. */
        INFO_PENDING = 0x02,/*< Include information about any pending items. */
        INFO_STORAGE = 0x04,/*< Include information about the storage. */
        INFO_ALL     = (INFO_RULES | INFO_PENDING | INFO_STORAGE)
    };

    typedef std::shared_ptr<CacheRules>     SCacheRules;
    typedef std::shared_ptr<StorageFactory> SStorageFactory;

    virtual ~Cache();

    json_t* show_json() const;

    const CacheConfig& config() const
    {
        return m_config;
    }

    /**
     * Create a token to be used for distinguishing between different
     * cache users within the same thread. An implementation that does
     * not need to differentiate between different users will return
     * NULL.
     *
     * @param psToken  On successful return, the new token.
     *                 NOTE: May be null.
     *
     * @return True if a token could be created (or if none had to be),
     *         false otherwise.
     */
    virtual bool create_token(std::shared_ptr<Token>* psToken) = 0;

    virtual void get_limits(Storage::Limits* pLimits) const = 0;

    virtual json_t* get_info(uint32_t what = INFO_ALL) const = 0;

    /**
     * Returns whether the results of a particular query should be stored.
     *
     * @param zDefaultDb  The current default database.
     * @param pQuery      Buffer containing a SELECT.
     *
     * @return A rules object, if the query should be stored, NULL otherwise.
     */
    const CacheRules* should_store(const char* zDefaultDb, const GWBUF* pQuery);

    /**
     * Specifies whether a particular SessioCache should refresh the data.
     *
     * @param key       The hashed key for a query.
     * @param pSession  The session cache asking.
     *
     * @return True, if the session cache should refresh the data.
     */
    virtual bool must_refresh(const CacheKey& key, const CacheFilterSession* pSession) = 0;

    /**
     * To inform the cache that a particular item has been updated upon request.
     *
     * @param key       The hashed key for a query.
     * @param pSession  The session cache informing.
     */
    virtual void refreshed(const CacheKey& key, const CacheFilterSession* pSession) = 0;

    /**
     * Returns a key for the statement. Takes the current config into account.
     *
     * @param user         The current user. Empty if a non-user specific cache is used.
     * @param host         The host of the current user. Empty if a non-user specific cache is used.
     * @param zDefault_db  The default database, can be NULL.
     * @param pQuery       A statement.
     * @param pKey         On output a key.
     *
     * @return CACHE_RESULT_OK if a key could be created.
     */
    virtual cache_result_t get_key(const std::string& user,
                                   const std::string& host,
                                   const char* zDefault_db,
                                   const GWBUF* pQuery,
                                   CacheKey* pKey) const;

    /**
     * Returns a key for the statement. Does not take the current config
     * into account.
     *
     * @param user         The current user. Empty if a non-user specific cache is used.
     * @param host         The host of the current user. Empty if a non-user specific cache is used.
     * @param zDefault_db  The default database, can be NULL.
     * @param pQuery       A statement.
     * @param pKey         On output a key.
     *
     * @return CACHE_RESULT_OK if a key could be created.
     */
    static cache_result_t get_default_key(const std::string& user,
                                          const std::string& host,
                                          const char* zDefault_db,
                                          const GWBUF* pQuery,
                                          CacheKey* pKey);

    static cache_result_t get_default_key(const char* zDefault_db,
                                          const GWBUF* pQuery,
                                          CacheKey* pKey)
    {
        return get_default_key(std::string(), std::string(), zDefault_db, pQuery, pKey);
    }

    /**
     * See @Storage::get_value
     */
    virtual
    cache_result_t get_value(Token* pToken,
                             const CacheKey& key,
                             uint32_t flags,
                             uint32_t soft_ttl,
                             uint32_t hard_ttl,
                             GWBUF** ppValue,
                             const std::function<void (cache_result_t, GWBUF*)>& cb = nullptr) const = 0;

    /**
     * See @Storage::put_value
     */
    virtual cache_result_t put_value(Token* pToken,
                                     const CacheKey& key,
                                     const std::vector<std::string>& invalidation_words,
                                     const GWBUF* pValue,
                                     const std::function<void (cache_result_t)>& cb = nullptr) = 0;

    /**
     * See @Storage::del_value
     */
    virtual cache_result_t del_value(Token* pToken,
                                     const CacheKey& key,
                                     const std::function<void (cache_result_t)>& cb = nullptr) = 0;

    /**
     * See @Storage::invalidate
     */
    virtual cache_result_t invalidate(Token* pToken,
                                      const std::vector<std::string>& words,
                                      const std::function<void (cache_result_t)>& cb = nullptr) = 0;

    /**
     * See @Storage::clear
     */
    virtual cache_result_t clear(Token* pToken) = 0;

    /**
     * Returns the monotonic time, expressed in milliseconds, since an
     * unspecified starting point.
     *
     * @return The time.
     */
    static uint64_t time_ms();

protected:
    Cache(const std::string& name,
          const CacheConfig* pConfig,
          const std::vector<SCacheRules>& rules,
          SStorageFactory sFactory);

    static bool get_storage_factory(const CacheConfig& config,
                                    std::vector<SCacheRules>* pRules,
                                    StorageFactory** ppFactory);

    json_t* do_get_info(uint32_t what) const;

private:
    Cache(const Cache&);
    Cache& operator=(const Cache&);

protected:
    const std::string        m_name;    // The name of the instance; the section name in the config.
    const CacheConfig&       m_config;  // The configuration of the cache instance.
    std::vector<SCacheRules> m_rules;   // The rules of the cache instance.
    SStorageFactory          m_sFactory;// The storage factory.
};
