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
#include <maxscale/buffer.h>
#include <maxscale/filter.hh>
#include "cache.hh"
#include "cachefilter.h"
#include "cache_storage_api.h"

class CacheFilterSession : public maxscale::FilterSession
{
public:
    enum cache_session_state_t
    {
        CACHE_EXPECTING_RESPONSE,     // A select has been sent, and we are waiting for the response.
        CACHE_EXPECTING_FIELDS,       // A select has been sent, and we want more fields.
        CACHE_EXPECTING_ROWS,         // A select has been sent, and we want more rows.
        CACHE_EXPECTING_NOTHING,      // We are not expecting anything from the server.
        CACHE_EXPECTING_USE_RESPONSE, // A "USE DB" was issued.
        CACHE_IGNORING_RESPONSE,      // We are not interested in the data received from the server.
    };

    struct CACHE_RESPONSE_STATE
    {
        GWBUF* pData;        /**< Response data, possibly incomplete. */
        size_t length;       /**< Length of pData. */
        size_t nTotalFields; /**< The number of fields a resultset contains. */
        size_t nFields;      /**< How many fields we have received, <= n_totalfields. */
        size_t nRows;        /**< How many rows we have received. */
        size_t offset;       /**< Where we are in the response buffer. */
    };

    /**
     * Releases all resources held by the session cache.
     */
    ~CacheFilterSession();

    /**
     * Creates a CacheFilterSession instance.
     *
     * @param pCache     Pointer to the cache instance to which this session cache
     *                   belongs. Must remain valid for the lifetime of the CacheFilterSession
     *                   instance being created.
     * @param pSession   Pointer to the session this session cache instance is
     *                   specific for. Must remain valid for the lifetime of the CacheFilterSession
     *                   instance being created.
     *
     * @return A new instance or NULL if memory allocation fails.
     */
    static CacheFilterSession* Create(Cache* pCache, MXS_SESSION* pSession);

    /**
     * The session has been closed.
     */
    void close();

    /**
     * A request on its way to a backend is delivered to this function.
     *
     * @param pPacket  Buffer containing an MySQL protocol packet.
     */
    int routeQuery(GWBUF* pPacket);

    /**
     * A response on its way to the client is delivered to this function.
     *
     * @param pData Response data.
     */
    int clientReply(GWBUF* pPacket);

    /**
     * Print diagnostics of the session cache.
     */
    void diagnostics(DCB *dcb);

    /**
     * Print diagnostics of the session cache.
     */
    json_t* diagnostics_json() const;

private:
    int handle_expecting_fields();
    int handle_expecting_nothing();
    int handle_expecting_response();
    int handle_expecting_rows();
    int handle_expecting_use_response();
    int handle_ignoring_response();

    int send_upstream();

    void reset_response_state();

    bool log_decisions() const
    {
        return m_pCache->config().debug & CACHE_DEBUG_DECISIONS ? true : false;
    }

    void store_result();

    bool should_consult_cache(GWBUF* pPacket);

private:
    CacheFilterSession(MXS_SESSION* pSession, Cache* pCache, char* zDefaultDb);

    CacheFilterSession(const CacheFilterSession&);
    CacheFilterSession& operator = (const CacheFilterSession&);

private:
    cache_session_state_t m_state;       /**< What state is the session in, what data is expected. */
    Cache*                m_pCache;      /**< The cache instance the session is associated with. */
    CACHE_RESPONSE_STATE  m_res;         /**< The response state. */
    CACHE_KEY             m_key;         /**< Key storage. */
    char*                 m_zDefaultDb;  /**< The default database. */
    char*                 m_zUseDb;      /**< Pending default database. Needs server response. */
    bool                  m_refreshing;  /**< Whether the session is updating a stale cache entry. */
    bool                  m_is_read_only;/**< Whether the current trx has been read-only in pratice. */
};

