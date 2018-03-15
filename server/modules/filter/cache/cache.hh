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
#include <tr1/functional>
#include <tr1/memory>
#include <string>
#include <maxscale/buffer.h>
#include <maxscale/session.h>
#include "cachefilter.h"
#include "cache_storage_api.h"

class CacheFilterSession;
class StorageFactory;

class Cache
{
public:
    enum what_info_t
    {
        INFO_RULES   = 0x01, /*< Include information about the rules. */
        INFO_PENDING = 0x02, /*< Include information about any pending items. */
        INFO_STORAGE = 0x04, /*< Include information about the storage. */
        INFO_ALL     = (INFO_RULES | INFO_PENDING | INFO_STORAGE)
    };

    typedef std::tr1::shared_ptr<CacheRules> SCacheRules;
    typedef std::tr1::shared_ptr<StorageFactory> SStorageFactory;

    virtual ~Cache();

    void show(DCB* pDcb) const;
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
     * @return True of the result should be cached.
     */
    bool should_store(const char* zDefaultDb, const GWBUF* pQuery);

    /**
     * Returns whether cached results should be used.
     *
     * @param pSession  The session in question.
     *
     * @return True of cached results should be used.
     */
    bool should_use(const MXS_SESSION* pSession);

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
    virtual void refreshed(const CACHE_KEY& key,  const CacheFilterSession* pSession) = 0;

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
                           CACHE_KEY* pKey) const;

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
                                          CACHE_KEY* pKey);

    /**
     * See @Storage::get_value
     */
    virtual cache_result_t get_value(const CACHE_KEY& key,
                                     uint32_t flags,
                                     uint32_t soft_ttl,
                                     uint32_t hard_ttl,
                                     GWBUF** ppValue) const = 0;

    /**
     * See @Storage::put_value
     */
    virtual cache_result_t put_value(const CACHE_KEY& key, const GWBUF* pValue) = 0;

    /**
     * See @Storage::del_value
     */
    virtual cache_result_t del_value(const CACHE_KEY& key) = 0;

protected:
    Cache(const std::string&  name,
          const CACHE_CONFIG* pConfig,
          SCacheRules         sRules,
          SStorageFactory     sFactory);

    static bool Create(const CACHE_CONFIG& config,
                       CacheRules**        ppRules,
                       StorageFactory**    ppFactory);

    json_t* do_get_info(uint32_t what) const;

private:
    Cache(const Cache&);
    Cache& operator = (const Cache&);

protected:
    const std::string   m_name;     // The name of the instance; the section name in the config.
    const CACHE_CONFIG& m_config;   // The configuration of the cache instance.
    SCacheRules         m_sRules;   // The rules of the cache instance.
    SStorageFactory     m_sFactory; // The storage factory.
};
