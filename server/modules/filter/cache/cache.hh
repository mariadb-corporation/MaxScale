/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
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

typedef enum cache_selects
{
    CACHE_SELECTS_ASSUME_CACHEABLE,
    CACHE_SELECTS_VERIFY_CACHEABLE,
} cache_selects_t;

// Count
#define CACHE_ZDEFAULT_MAX_RESULTSET_ROWS "0"
// Bytes
#define CACHE_ZDEFAULT_MAX_RESULTSET_SIZE "0"
// Seconds
#define CACHE_ZDEFAULT_HARD_TTL "0"
// Seconds
#define CACHE_ZDEFAULT_SOFT_TTL "0"
// Integer value
#define CACHE_ZDEFAULT_DEBUG "0"
// Positive integer
#define CACHE_ZDEFAULT_MAX_COUNT "0"
// Positive integer
#define CACHE_ZDEFAULT_MAX_SIZE "0"
// Thread model
#define CACHE_ZDEFAULT_THREAD_MODEL "thread_specific"
const cache_thread_model CACHE_DEFAULT_THREAD_MODEL = CACHE_THREAD_MODEL_ST;
// Cacheable selects
#define CACHE_ZDEFAULT_SELECTS "assume_cacheable"
const cache_selects_t CACHE_DEFAULT_SELECTS = CACHE_SELECTS_ASSUME_CACHEABLE;
// Storage
#define CACHE_ZDEFAULT_STORAGE "storage_inmemory"
// Transaction behaviour
#define CACHE_ZDEFAULT_CACHE_IN_TRXS "all_transactions"
// Enabled
#define CACHE_ZDEFAULT_ENABLED "true"

typedef enum cache_in_trxs
{
    // Do NOT change the order. Code relies upon NEVER < READ_ONLY < ALL.
    CACHE_IN_TRXS_NEVER,
    CACHE_IN_TRXS_READ_ONLY,
    CACHE_IN_TRXS_ALL,
} cache_in_trxs_t;

typedef struct cache_config
{
    uint64_t max_resultset_rows;            /**< The maximum number of rows of a resultset for it to be
                                             * cached. */
    uint64_t             max_resultset_size;/**< The maximum size of a resultset for it to be cached. */
    char*                rules;             /**< Name of rules file. */
    char*                storage;           /**< Name of storage module. */
    char*                storage_options;   /**< Raw options for storage module. */
    char**               storage_argv;      /**< Cooked options for storage module. */
    int                  storage_argc;      /**< Number of cooked options. */
    uint32_t             hard_ttl;          /**< Hard time to live. */
    uint32_t             soft_ttl;          /**< Soft time to live. */
    uint64_t             max_count;         /**< Maximum number of entries in the cache.*/
    uint64_t             max_size;          /**< Maximum size of the cache.*/
    uint32_t             debug;             /**< Debug settings. */
    cache_thread_model_t thread_model;      /**< Thread model. */
    cache_selects_t      selects;           /**< Assume/verify that selects are cacheable. */
    cache_in_trxs_t      cache_in_trxs;     /**< To cache or not to cache inside transactions. */
    bool                 enabled;           /**< Whether the cache is enabled or not. */
} CACHE_CONFIG;

class Cache
{
public:
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

    void    show(DCB* pDcb) const;
    json_t* show_json() const;

    const CACHE_CONFIG& config() const
    {
        return m_config;
    }

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
    virtual bool must_refresh(const CACHE_KEY& key, const CacheFilterSession* pSession) = 0;

    /**
     * To inform the cache that a particular item has been updated upon request.
     *
     * @param key       The hashed key for a query.
     * @param pSession  The session cache informing.
     */
    virtual void refreshed(const CACHE_KEY& key, const CacheFilterSession* pSession) = 0;

    /**
     * Returns a key for the statement. Takes the current config into account.
     *
     * @param zDefault_db  The default database, can be NULL.
     * @param pQuery       A statement.
     * @param pKey         On output a key.
     *
     * @return CACHE_RESULT_OK if a key could be created.
     */
    cache_result_t get_key(const char* zDefault_db,
                           const GWBUF* pQuery,
                           CACHE_KEY*   pKey) const;

    /**
     * Returns a key for the statement. Does not take the current config
     * into account.
     *
     * @param zDefault_db  The default database, can be NULL.
     * @param pQuery       A statement.
     * @param pKey         On output a key.
     *
     * @return CACHE_RESULT_OK if a key could be created.
     */
    static cache_result_t get_default_key(const char* zDefault_db,
                                          const GWBUF* pQuery,
                                          CACHE_KEY*   pKey);

    /**
     * See @Storage::get_value
     */
    virtual cache_result_t get_value(const CACHE_KEY& key,
                                     uint32_t flags,
                                     uint32_t soft_ttl,
                                     uint32_t hard_ttl,
                                     GWBUF**  ppValue) const = 0;

    /**
     * See @Storage::put_value
     */
    virtual cache_result_t put_value(const CACHE_KEY& key, const GWBUF* pValue) = 0;

    /**
     * See @Storage::del_value
     */
    virtual cache_result_t del_value(const CACHE_KEY& key) = 0;

protected:
    Cache(const std::string& name,
          const CACHE_CONFIG* pConfig,
          const std::vector<SCacheRules>& rules,
          SStorageFactory sFactory);

    static bool Create(const CACHE_CONFIG& config,
                       std::vector<SCacheRules>* pRules,
                       StorageFactory** ppFactory);

    json_t* do_get_info(uint32_t what) const;

private:
    Cache(const Cache&);
    Cache& operator=(const Cache&);

protected:
    const std::string        m_name;    // The name of the instance; the section name in the config.
    const CACHE_CONFIG&      m_config;  // The configuration of the cache instance.
    std::vector<SCacheRules> m_rules;   // The rules of the cache instance.
    SStorageFactory          m_sFactory;// The storage factory.
};
