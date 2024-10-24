/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <deque>
#include <unordered_set>
#include <maxscale/buffer.hh>
#include <maxscale/filter.hh>
#include "sessioncache.hh"

class CacheFilterSession : public maxscale::FilterSession
{
    CacheFilterSession(const CacheFilterSession&);
    CacheFilterSession& operator=(const CacheFilterSession&);

public:
    enum cache_session_state_t
    {
        CACHE_EXPECTING_RESPONSE,       // A select has been sent, and we are waiting for the response.
        CACHE_EXPECTING_NOTHING,        // We are not expecting anything from the server.
        CACHE_EXPECTING_USE_RESPONSE,   // A "USE DB" was issued.
        CACHE_STORING_RESPONSE,         // A select has been sent, and we are storing the data.
        CACHE_IGNORING_RESPONSE,        // We are not interested in the data received from the server.
    };

    /**
     * Releases all resources held by the session cache.
     */
    ~CacheFilterSession();

    /**
     * @return The cache config.
     */
    const CacheConfig& config() const
    {
        return m_sCache->config();
    }

    /**
     * @return The current user if user specific cache or empty string if not.
     */
    const std::string& user() const;

    /**
     * @return The current host if user specific cache or empty string if not.
     */
    const std::string& host() const;

    /**
     * @return Current db or NULL if there is not one.
     */
    const char* default_db() const
    {
        return m_zDefaultDb;
    }

    /**
     * @see Cache::get_value
     */
    cache_result_t get_value(const CacheKey& key,
                             uint32_t flags,
                             GWBUF* pValue,
                             const std::function<void (cache_result_t, GWBUF&&)>& cb) const
    {
        return m_sCache->get_value(key, flags, m_soft_ttl, m_hard_ttl, pValue, cb);
    }

    /**
     * @see Cache::put_value
     */
    cache_result_t put_value(const CacheKey& key,
                             const std::vector<std::string>& invalidation_words,
                             const GWBUF& value,
                             const std::function<void (cache_result_t)>& cb) const
    {
        return m_sCache->put_value(key, invalidation_words, value, cb);
    }

    /**
     * @see Cache::invalidate
     */
    cache_result_t invalidate(const std::vector<std::string>& words,
                              const std::function<void (cache_result_t)>& cb)
    {
        return m_sCache->invalidate(words, cb);
    }

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
    static CacheFilterSession* create(std::unique_ptr<SessionCache> sCache,
                                      MXS_SESSION* pSession,
                                      SERVICE* pService);

    /**
     * A request on its way to a backend is delivered to this function.
     *
     * @param pPacket  Buffer containing an MySQL protocol packet.
     */
    bool routeQuery(GWBUF&& packet) override;

    bool clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

    /**
     * Print diagnostics of the session cache.
     */
    json_t* diagnostics() const;

private:
    void handle_expecting_nothing(const mxs::Reply& reply);
    void handle_expecting_use_response(const mxs::Reply& reply);
    void handle_storing_response(const mxs::ReplyRoute& down, const mxs::Reply& reply);
    void handle_ignoring_response();

    void store_and_prepare_response(const mxs::ReplyRoute& down, const mxs::Reply& reply);
    void prepare_response();
    int  flush_response(const mxs::ReplyRoute& down, const mxs::Reply& reply);

    void reset_response_state();

    bool log_decisions() const
    {
        return m_sCache->config().debug & CACHE_DEBUG_DECISIONS ? true : false;
    }


    enum cache_action_t
    {
        CACHE_IGNORE           = 0,
        CACHE_USE              = 1,
        CACHE_POPULATE         = 2,
        CACHE_USE_AND_POPULATE = (CACHE_USE | CACHE_POPULATE)
    };

    static bool should_use(cache_action_t action)
    {
        return action & CACHE_USE ? true : false;
    }

    static bool should_populate(cache_action_t action)
    {
        return action & CACHE_POPULATE ? true : false;
    }

    cache_action_t get_cache_action(const GWBUF& packet);

    void update_table_names(const GWBUF& packet);

    enum routing_action_t
    {
        ROUTING_ABORT,      /**< Abort normal routing activity, data is coming from cache. */
        ROUTING_CONTINUE,   /**< Continue normal routing activity. */
    };

    routing_action_t route_COM_QUERY(GWBUF&& packet);
    routing_action_t route_SELECT(cache_action_t action, const CacheRules& rules, GWBUF&& packet);

    char* set_cache_populate(const char* zName,
                             const char* pValue_begin,
                             const char* pValue_end);
    char* set_cache_use(const char* zName,
                        const char* pValue_begin,
                        const char* pValue_end);
    char* set_cache_soft_ttl(const char* zName,
                             const char* pValue_begin,
                             const char* pValue_end);
    char* set_cache_hard_ttl(const char* zName,
                             const char* pValue_begin,
                             const char* pValue_end);

    static char* set_cache_populate(void* pContext,
                                    const char* zName,
                                    const char* pValue_begin,
                                    const char* pValue_end);
    static char* set_cache_use(void* pContext,
                               const char* zName,
                               const char* pValue_begin,
                               const char* pValue_end);
    static char* set_cache_soft_ttl(void* pContext,
                                    const char* zName,
                                    const char* pValue_begin,
                                    const char* pValue_end);
    static char* set_cache_hard_ttl(void* pContext,
                                    const char* zName,
                                    const char* pValue_begin,
                                    const char* pValue_end);

    using SSessionCache = std::shared_ptr<SessionCache>;

    bool put_value_handler(cache_result_t result,
                           const mxs::ReplyRoute& down,
                           const mxs::Reply& reply);
    void             del_value_handler(cache_result_t result);
    routing_action_t get_value_handler(cache_result_t result);
    void             invalidate_handler(cache_result_t result);
    int              client_reply_post_process(const mxs::ReplyRoute& down,
                                               const mxs::Reply& reply);
    void clear_cache();

    int continue_routing(GWBUF&& packet);

    void ready_for_another_call();

private:
    CacheFilterSession(MXS_SESSION* pSession,
                       SERVICE* pService,
                       std::unique_ptr<SessionCache> sCache,
                       char* zDefaultDb);

private:
    using Tables = std::unordered_set<std::string>;
    using SCacheFilterSession = std::shared_ptr<CacheFilterSession>;

    SCacheFilterSession     m_sThis;          /**< Shared pointer to this. */
    cache_session_state_t   m_state;          /**< What state is the session in, what data is expected. */
    SSessionCache           m_sCache;         /**< The cache instance the session is associated with. */
    GWBUF                   m_res;            /**< The response buffer. */
    GWBUF                   m_next_response;  /**< The next response routed to the client. */
    CacheKey                m_key;            /**< Key storage. */
    char*                   m_zDefaultDb;     /**< The default database. */
    char*                   m_zUseDb;         /**< Pending default database. Needs server response. */
    bool                    m_refreshing;     /**< Whether the session is updating a stale cache entry. */
    bool                    m_is_read_only;   /**< Whether the current trx has been read-only in practice. */
    bool                    m_use;            /**< Whether the cache should be used in this session. */
    bool                    m_populate;       /**< Whether the cache should be populated in this session. */
    uint32_t                m_soft_ttl;       /**< The soft TTL used in the session. */
    uint32_t                m_hard_ttl;       /**< The hard TTL used in the session. */
    bool                    m_invalidate;     /**< Whether invalidation should be performed. */
    bool                    m_invalidate_now; /**< Should invalidation be done at next response. */
    Tables                  m_tables;         /**< Tables selected or modified. */
    bool                    m_clear_cache;    /**< Whether the entire cache should be cleared. */
    bool                    m_user_specific;  /**< Whether a user specific cache should be used. */
    std::deque<GWBUF>       m_queued_packets; /**< Queued statements, waiting for current to finish. */
    bool                    m_processing;     /**< Is query processing on-going. */
    bool                    m_load_active {false};
};
