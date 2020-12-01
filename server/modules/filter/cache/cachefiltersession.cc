/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-26
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "cachefiltersession.hh"
#include <new>
#include <maxbase/alloc.h>
#include <maxbase/pretty_print.hh>
#include <maxscale/modutil.hh>
#include <maxscale/mysql_utils.hh>
#include <maxscale/protocol/mariadb/query_classifier.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include "storage.hh"

namespace
{

inline bool cache_max_resultset_rows_exceeded(const CacheConfig& config, int64_t rows)
{
    return config.max_resultset_rows == 0 ? false : rows > config.max_resultset_rows;
}

inline bool cache_max_resultset_size_exceeded(const CacheConfig& config, int64_t size)
{
    return config.max_resultset_size == 0 ? false : size > config.max_resultset_size;
}
}

namespace
{

const char SV_MAXSCALE_CACHE_POPULATE[] = "@maxscale.cache.populate";
const char SV_MAXSCALE_CACHE_USE[] = "@maxscale.cache.use";
const char SV_MAXSCALE_CACHE_SOFT_TTL[] = "@maxscale.cache.soft_ttl";
const char SV_MAXSCALE_CACHE_HARD_TTL[] = "@maxscale.cache.hard_ttl";

const char* NON_CACHEABLE_FUNCTIONS[] =
{
    "benchmark",
    "connection_id",
    "convert_tz",
    "curdate",
    "current_date",
    "current_timestamp",
    "curtime",
    "database",
    "encrypt",
    "found_rows",
    "get_lock",
    "is_free_lock",
    "is_used_lock",
    "last_insert_id",
    "load_file",
    "localtime",
    "localtimestamp",
    "master_pos_wait",
    "now",
    "rand",
    "release_lock",
    "session_user",
    "sleep",
    "sysdate",
    "system_user",
    "unix_timestamp",
    "user",
    "uuid",
    "uuid_short",
};

const char* NON_CACHEABLE_VARIABLES[] =
{
    "current_date",
    "current_timestamp",
    "localtime",
    "localtimestamp",
};

const size_t N_NON_CACHEABLE_FUNCTIONS = sizeof(NON_CACHEABLE_FUNCTIONS) / sizeof(NON_CACHEABLE_FUNCTIONS[0]);
const size_t N_NON_CACHEABLE_VARIABLES = sizeof(NON_CACHEABLE_VARIABLES) / sizeof(NON_CACHEABLE_VARIABLES[0]);

int compare_name(const void* pLeft, const void* pRight)
{
    return strcasecmp((const char*)pLeft, *(const char**)pRight);
}

inline bool uses_name(const char* zName, const char** pzNames, size_t nNames)
{
    return bsearch(zName, pzNames, nNames, sizeof(const char*), compare_name) != NULL;
}

bool uses_non_cacheable_function(GWBUF* pPacket)
{
    bool rv = false;

    const QC_FUNCTION_INFO* pInfo;
    size_t nInfos;

    qc_get_function_info(pPacket, &pInfo, &nInfos);

    const QC_FUNCTION_INFO* pEnd = pInfo + nInfos;

    while (!rv && (pInfo != pEnd))
    {
        rv = uses_name(pInfo->name, NON_CACHEABLE_FUNCTIONS, N_NON_CACHEABLE_FUNCTIONS);

        ++pInfo;
    }

    return rv;
}

bool uses_non_cacheable_variable(GWBUF* pPacket)
{
    bool rv = false;

    const QC_FIELD_INFO* pInfo;
    size_t nInfos;

    qc_get_field_info(pPacket, &pInfo, &nInfos);

    const QC_FIELD_INFO* pEnd = pInfo + nInfos;

    while (!rv && (pInfo != pEnd))
    {
        rv = uses_name(pInfo->column, NON_CACHEABLE_VARIABLES, N_NON_CACHEABLE_VARIABLES);

        ++pInfo;
    }

    return rv;
}
}

namespace
{

enum class StatementType
{
    SELECT,
    DUPSERT,    // DELETE, UPDATE, INSERT
    UNKNOWN
};

StatementType get_statement_type(GWBUF* pStmt)
{
    StatementType type = StatementType::UNKNOWN;

    char* pSql;
    int len;

    MXB_AT_DEBUG(int rc = ) modutil_extract_SQL(pStmt, &pSql, &len);
    mxb_assert(rc == 1);

    char* pSql_end = pSql + len;

    pSql = modutil_MySQL_bypass_whitespace(pSql, len);

    static const char DELETE[] = "DELETE";
    static const char INSERT[] = "INSERT";
    static const char SELECT[] = "SELECT";
    static const char UPDATE[] = "UPDATE";

    const char* pKey = nullptr;
    const char* pKey_end = nullptr;

    if (pSql_end > pSql)
    {
        switch (*pSql)
        {
        case 'D':
        case 'd':
            type = StatementType::DUPSERT;
            pKey = DELETE;
            pKey_end = pKey + sizeof(DELETE) - 1;
            break;

        case 'I':
        case 'i':
            type = StatementType::DUPSERT;
            pKey = INSERT;
            pKey_end = pKey + sizeof(INSERT) - 1;
            break;

        case 'S':
        case 's':
            type = StatementType::SELECT;
            pKey = SELECT;
            pKey_end = pKey + sizeof(SELECT) - 1;
            break;

        case 'U':
        case 'u':
            type = StatementType::DUPSERT;
            pKey = UPDATE;
            pKey_end = pKey + sizeof(UPDATE) - 1;
            break;

        default:
            break;
        }

        if (type != StatementType::UNKNOWN)
        {
            ++pKey;
            ++pSql;

            while ((pSql < pSql_end) && (pKey < pKey_end) && (toupper(*pSql) == *pKey))
            {
                ++pSql;
                ++pKey;
            }

            if ((pKey == pKey_end) && ((pSql == pSql_end) || !isalpha(*pSql)))
            {
                // All is fine; either the statement only contain the keyword (so syntactically
                // the statement is erroenous) or the keyword was followed by something else
                // than an alhpanumeric character, e.g. whitespace.
            }
            else
            {
                type = StatementType::UNKNOWN;
            }
        }
    }

    return type;
}
}

CacheFilterSession::CacheFilterSession(MXS_SESSION* pSession,
                                       SERVICE* pService,
                                       std::unique_ptr<SessionCache> sCache,
                                       char* zDefaultDb)
    : maxscale::FilterSession(pSession, pService)
    , m_sThis(SCacheFilterSession(this, [](auto ptr) {
                                  }))
    , m_state(CACHE_EXPECTING_NOTHING)
    , m_sCache(std::move(sCache))
    , m_next_response(nullptr)
    , m_zDefaultDb(zDefaultDb)
    , m_zUseDb(NULL)
    , m_refreshing(false)
    , m_is_read_only(true)
    , m_use(m_sCache->config().enabled)
    , m_populate(m_sCache->config().enabled)
    , m_soft_ttl(m_sCache->config().soft_ttl.count())
    , m_hard_ttl(m_sCache->config().hard_ttl.count())
    , m_invalidate(m_sCache->config().invalidate != CACHE_INVALIDATE_NEVER)
    , m_invalidate_now(false)
    , m_clear_cache(false)
    , m_user_specific(m_sCache->config().users == CACHE_USERS_ISOLATED)
{
    m_key.data_hash = 0;
    m_key.full_hash = 0;

    reset_response_state();

    if (!session_add_variable(pSession,
                              SV_MAXSCALE_CACHE_POPULATE,
                              &CacheFilterSession::set_cache_populate,
                              this))
    {
        mxb_assert(!true);
        MXS_ERROR("Could not add MaxScale user variable '%s', dynamically "
                  "enabling/disabling the populating of the cache is not possible.",
                  SV_MAXSCALE_CACHE_POPULATE);
    }

    if (!session_add_variable(pSession,
                              SV_MAXSCALE_CACHE_USE,
                              &CacheFilterSession::set_cache_use,
                              this))
    {
        mxb_assert(!true);
        MXS_ERROR("Could not add MaxScale user variable '%s', dynamically "
                  "enabling/disabling the using of the cache not possible.",
                  SV_MAXSCALE_CACHE_USE);
    }

    if (!session_add_variable(pSession,
                              SV_MAXSCALE_CACHE_SOFT_TTL,
                              &CacheFilterSession::set_cache_soft_ttl,
                              this))
    {
        mxb_assert(!true);
        MXS_ERROR("Could not add MaxScale user variable '%s', dynamically "
                  "setting the soft TTL not possible.",
                  SV_MAXSCALE_CACHE_SOFT_TTL);
    }

    if (!session_add_variable(pSession,
                              SV_MAXSCALE_CACHE_HARD_TTL,
                              &CacheFilterSession::set_cache_hard_ttl,
                              this))
    {
        mxb_assert(!true);
        MXS_ERROR("Could not add MaxScale user variable '%s', dynamically "
                  "setting the hard TTL not possible.",
                  SV_MAXSCALE_CACHE_HARD_TTL);
    }
}

CacheFilterSession::~CacheFilterSession()
{
    MXS_FREE(m_zUseDb);
    MXS_FREE(m_zDefaultDb);
}

// static
CacheFilterSession* CacheFilterSession::create(std::unique_ptr<SessionCache> sCache,
                                               MXS_SESSION* pSession,
                                               SERVICE* pService)
{
    CacheFilterSession* pCacheFilterSession = NULL;
    auto db = static_cast<MYSQL_session*>(pSession->protocol_data())->current_db;
    char* zDefaultDb = NULL;

    if (!db.empty())
    {
        zDefaultDb = MXS_STRDUP(db.c_str());
    }

    if (db.empty() || zDefaultDb)
    {
        pCacheFilterSession = new(std::nothrow) CacheFilterSession(pSession,
                                                                   pService,
                                                                   std::move(sCache),
                                                                   zDefaultDb);

        if (!pCacheFilterSession)
        {
            MXS_FREE(zDefaultDb);
        }
    }

    return pCacheFilterSession;
}

int CacheFilterSession::routeQuery(GWBUF* pPacket)
{
    uint8_t* pData = static_cast<uint8_t*>(GWBUF_DATA(pPacket));

    // All of these should be guaranteed by RCAP_TYPE_TRANSACTION_TRACKING
    mxb_assert(gwbuf_is_contiguous(pPacket));
    mxb_assert(GWBUF_LENGTH(pPacket) >= MYSQL_HEADER_LEN + 1);
    mxb_assert(MYSQL_GET_PAYLOAD_LEN(pData) + MYSQL_HEADER_LEN == GWBUF_LENGTH(pPacket));

    routing_action_t action = ROUTING_CONTINUE;

    reset_response_state();
    m_state = CACHE_IGNORING_RESPONSE;

    int rv = 1;

    switch ((int)MYSQL_GET_COMMAND(pData))
    {
    case MXS_COM_INIT_DB:
        {
            mxb_assert(!m_zUseDb);
            size_t len = MYSQL_GET_PAYLOAD_LEN(pData) - 1;      // Remove the command byte.
            m_zUseDb = (char*)MXS_MALLOC(len + 1);

            if (m_zUseDb)
            {
                memcpy(m_zUseDb, (char*)(pData + MYSQL_HEADER_LEN + 1), len);
                m_zUseDb[len] = 0;
                m_state = CACHE_EXPECTING_USE_RESPONSE;
            }
            else
            {
                // Memory allocation failed. We need to remove the default database to
                // prevent incorrect cache entries, since we won't know what the
                // default db is. But we only need to do that if "USE <db>" really
                // succeeds. The right thing will happen by itself in
                // handle_expecting_use_response(); if OK is returned, default_db will
                // become NULL, if ERR, default_db will not be changed.
            }
        }
        break;

    case MXS_COM_STMT_PREPARE:
        if (log_decisions())
        {
            MXS_NOTICE("COM_STMT_PREPARE, ignoring.");
        }
        break;

    case MXS_COM_STMT_EXECUTE:
        if (log_decisions())
        {
            MXS_NOTICE("COM_STMT_EXECUTE, ignoring.");
        }
        break;

    case MXS_COM_QUERY:
        action = route_COM_QUERY(pPacket);
        break;

    default:
        break;
    }

    if (action == ROUTING_CONTINUE)
    {
        rv = continue_routing(pPacket);
    }

    return rv;
}

int CacheFilterSession::client_reply_post_process(GWBUF* pData,
                                                  const mxs::ReplyRoute& down,
                                                  const mxs::Reply& reply)
{
    switch (m_state)
    {
    case CACHE_EXPECTING_NOTHING:
        handle_expecting_nothing(reply);
        break;

    case CACHE_EXPECTING_USE_RESPONSE:
        handle_expecting_use_response(reply);
        break;

    case CACHE_STORING_RESPONSE:
        handle_storing_response(down, reply);
        break;

    case CACHE_IGNORING_RESPONSE:
        handle_ignoring_response();
        break;

    default:
        MXS_ERROR("Internal cache logic broken, unexpected state: %d", m_state);
        mxb_assert(!true);
        prepare_response();
        m_state = CACHE_IGNORING_RESPONSE;
    }

    return flush_response(down, reply);
}

void CacheFilterSession::clear_cache()
{
    if (m_sCache->clear() != CACHE_RESULT_OK)
    {
        MXS_ERROR("Could not clear the cache, which is now in "
                  "inconsistent state. Caching will now be disabled.");
        m_use = false;
        m_populate = false;
    }
}

void CacheFilterSession::invalidate_handler(cache_result_t result)
{
    if (CACHE_RESULT_IS_OK(result))
    {
        if (log_decisions())
        {
            MXS_NOTICE("Cache successfully invalidated.");
        }
    }
    else
    {
        MXS_WARNING("Failed to invalidate individual cache entries, "
                    "the cache will now be cleared.");
        clear_cache();
    }
}

int CacheFilterSession::clientReply(GWBUF* pData, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    m_res = m_res ? gwbuf_append(m_res, pData) : pData;

    if (m_state == CACHE_EXPECTING_RESPONSE)
    {
        if (reply.is_resultset())
        {
            m_state = CACHE_STORING_RESPONSE;
        }
        else
        {
            // A failed SELECT
            m_tables.clear();
            m_state = CACHE_IGNORING_RESPONSE;
        }
    }

    int rv = 1;

    bool post_process = true;

    if (m_invalidate_now)
    {
        // The response to either a COMMIT, or to UPDATE/DELETE/INSERT with
        // autcommit being true.

        if (reply.is_complete())
        {
            // Usually it will be an OK, but we are future proof by accepting result sets as well.
            if (reply.is_ok() || reply.is_resultset())
            {
                if (!m_clear_cache)
                {
                    std::vector<std::string> invalidation_words;
                    std::copy(m_tables.begin(), m_tables.end(), std::back_inserter(invalidation_words));

                    std::weak_ptr<CacheFilterSession> sWeak {m_sThis};

                    cache_result_t result =
                        m_sCache->invalidate(invalidation_words,
                                             [sWeak, pData, down, reply](cache_result_t result) {
                                                 std::shared_ptr<CacheFilterSession> sThis = sWeak.lock();

                                                 if (sThis)
                                                 {
                                                     sThis->invalidate_handler(result);

                                                     sThis->client_reply_post_process(pData, down, reply);
                                                 }
                                                 else
                                                 {
                                                    // Ok, so the session was terminated before
                                                    // we got a reply.
                                                     gwbuf_free(pData);
                                                 }
                                             });

                    if (CACHE_RESULT_IS_PENDING(result))
                    {
                        post_process = false;
                    }
                    else
                    {
                        invalidate_handler(result);
                    }
                }
                else
                {
                    clear_cache();
                }
            }

            // Irrespective of whether the invalidation is synchronous or asynchronous,
            // the following state variables can be reset. If synchronous they must be
            // reset, if asynchronous it does not matter whether they are reset now or
            // only after the callback is called.
            m_tables.clear();
            m_invalidate_now = false;
            m_clear_cache = false;
        }
    }

    if (post_process)
    {
        rv = client_reply_post_process(pData, down, reply);
    }

    return rv;
}

json_t* CacheFilterSession::diagnostics() const
{
    // Not printing anything. Session of the same instance share the same cache, in
    // which case the same information would be printed once per session, or all
    // threads (but not sessions) share the same cache, in which case the output
    // would be nonsensical.
    return NULL;
}

/**
 * Called when data is received (even if nothing is expected) from the server.
 */
void CacheFilterSession::handle_expecting_nothing(const mxs::Reply& reply)
{
    mxb_assert(m_state == CACHE_EXPECTING_NOTHING);
    mxb_assert(m_res);

    if (reply.error())
    {
        MXS_INFO("Error packet received from backend: %s", reply.error().message().c_str());
    }
    else
    {
        MXS_WARNING("Received data from the backend although filter is expecting nothing.");
        mxb_assert(!true);
    }

    prepare_response();
}

/**
 * Called when a response to a "USE db" is received from the server.
 */
void CacheFilterSession::handle_expecting_use_response(const mxs::Reply& reply)
{
    mxb_assert(m_state == CACHE_EXPECTING_USE_RESPONSE);
    mxb_assert(m_res);
    mxb_assert(reply.is_complete());

    if (reply.error())
    {
        // The USE failed which means the default database did not change
        MXS_FREE(m_zUseDb);
        m_zUseDb = NULL;
    }
    else
    {
        mxb_assert(mxs_mysql_get_command(m_res) == MYSQL_REPLY_OK);
        // In case m_zUseDb could not be allocated in routeQuery(), we will
        // in fact reset the default db here. That's ok as it will prevent broken
        // entries in the cache.
        MXS_FREE(m_zDefaultDb);
        m_zDefaultDb = m_zUseDb;
        m_zUseDb = NULL;
    }

    prepare_response();
    m_state = CACHE_IGNORING_RESPONSE;
}

/**
 * Called when a resultset is being collected.
 */
void CacheFilterSession::handle_storing_response(const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxb_assert(m_state == CACHE_STORING_RESPONSE);
    mxb_assert(m_res);

    if (cache_max_resultset_size_exceeded(m_sCache->config(), reply.size()))
    {
        if (log_decisions())
        {
            MXS_NOTICE("Current resultset size exceeds maximum allowed size %s. Not caching.",
                       mxb::pretty_size(m_sCache->config().max_resultset_size).c_str());
        }

        prepare_response();
        m_state = CACHE_IGNORING_RESPONSE;
    }
    else if (cache_max_resultset_rows_exceeded(m_sCache->config(), reply.rows_read()))
    {
        if (log_decisions())
        {
            MXS_NOTICE("Max rows %lu reached, not caching result.", reply.rows_read());
        }

        prepare_response();
        m_state = CACHE_IGNORING_RESPONSE;
    }
    else if (reply.is_complete())
    {
        if (log_decisions())
        {
            MXS_NOTICE("Result collected, rows: %lu, size: %s", reply.rows_read(),
                       mxb::pretty_size(reply.size()).c_str());
        }

        store_and_prepare_response(down, reply);
        m_state = CACHE_EXPECTING_NOTHING;
    }
}

/**
 * Called when all data from the server is ignored.
 */
void CacheFilterSession::handle_ignoring_response()
{
    mxb_assert(m_state == CACHE_IGNORING_RESPONSE);
    mxb_assert(m_res);

    prepare_response();
}

/**
 * Send data upstream.
 *
 * Queues the current response for forwarding to the upstream component.
 */
void CacheFilterSession::prepare_response()
{
    mxb_assert(m_res);
    mxb_assert(!m_next_response);
    m_next_response = m_res;
    m_res = NULL;
}

/**
 * Sends data to the client, if there is something to send.
 */
int CacheFilterSession::flush_response(const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    GWBUF* next_response = m_next_response;
    m_next_response = nullptr;
    int rv = 1;

    if (next_response)
    {
        rv = FilterSession::clientReply(next_response, down, reply);
    }

    return rv;
}

/**
 * Reset cache response state
 */
void CacheFilterSession::reset_response_state()
{
    m_res = NULL;
}

/**
 * Store the data.
 */
void CacheFilterSession::store_and_prepare_response(const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxb_assert(m_res);

    GWBUF* pData = gwbuf_make_contiguous(m_res);
    MXS_ABORT_IF_NULL(pData);

    m_res = pData;

    std::vector<std::string> invalidation_words;

    if (m_invalidate)
    {
        std::copy(m_tables.begin(), m_tables.end(), std::back_inserter(invalidation_words));
        m_tables.clear();
    }

    std::weak_ptr<CacheFilterSession> sWeak {m_sThis};

    cache_result_t result = m_sCache->put_value(m_key, invalidation_words, m_res,
                                                [sWeak, down, reply](cache_result_t result) {
                                                    auto sThis = sWeak.lock();

                                                    // If we do not have an sThis, then the session
                                                    // has been terminated.
                                                    if (sThis)
                                                    {
                                                        if (sThis->put_value_handler(result, down, reply))
                                                        {
                                                            sThis->flush_response(down, reply);
                                                        }
                                                    }
                                                });

    if (!CACHE_RESULT_IS_PENDING(result))
    {
        put_value_handler(result, down, reply);
    }

    // Whether or not the result is returned immediately or later, we proceed the
    // same way.

    if (m_refreshing)
    {
        m_sCache->refreshed(m_key, this);
        m_refreshing = false;
    }
}

/**
 * Whether the cache should be consulted.
 *
 * @param pParam The GWBUF being handled.
 *
 * @return Enum value indicating appropriate action.
 */
CacheFilterSession::cache_action_t CacheFilterSession::get_cache_action(GWBUF* pPacket)
{
    cache_action_t action = CACHE_IGNORE;

    m_invalidate_now = false;

    if (m_use || m_populate)
    {
        uint32_t type_mask = qc_get_trx_type_mask(pPacket);     // Note, only trx-related type mask

        const char* zPrimary_reason = NULL;
        const char* zSecondary_reason = "";
        const CacheConfig& config = m_sCache->config();
        auto protocol_data = m_pSession->protocol_data();

        if (qc_query_is_type(type_mask, QUERY_TYPE_BEGIN_TRX))
        {
            if (log_decisions())
            {
                zPrimary_reason = "transaction start";
            }

            // When a transaction is started, we initially assume it is read-only.
            m_is_read_only = true;
        }
        else if (!protocol_data->is_trx_active())
        {
            if (log_decisions())
            {
                zPrimary_reason = "no transaction";
            }
            action = CACHE_USE_AND_POPULATE;
        }
        else if (protocol_data->is_trx_read_only())
        {
            if (config.cache_in_trxs >= CACHE_IN_TRXS_READ_ONLY)
            {
                if (log_decisions())
                {
                    zPrimary_reason = "explicitly read-only transaction";
                }
                action = CACHE_USE_AND_POPULATE;
            }
            else
            {
                mxb_assert(config.cache_in_trxs == CACHE_IN_TRXS_NEVER);

                if (log_decisions())
                {
                    zPrimary_reason = "populating but not using cache inside read-only transactions";
                }
                action = CACHE_POPULATE;
            }
        }
        else if (m_is_read_only)
        {
            // There is a transaction and it is *not* explicitly read-only,
            // although so far there has only been SELECTs.

            if (config.cache_in_trxs >= CACHE_IN_TRXS_ALL)
            {
                if (log_decisions())
                {
                    zPrimary_reason = "ordinary transaction that has so far been read-only";
                }
                action = CACHE_USE_AND_POPULATE;
            }
            else
            {
                mxb_assert((config.cache_in_trxs == CACHE_IN_TRXS_NEVER)
                           || (config.cache_in_trxs == CACHE_IN_TRXS_READ_ONLY));

                if (log_decisions())
                {
                    zPrimary_reason =
                        "populating but not using cache inside transaction that is not "
                        "explicitly read-only, but that has used only SELECTs sofar";
                }
                action = CACHE_POPULATE;
            }
        }
        else
        {
            if (log_decisions())
            {
                zPrimary_reason = "ordinary transaction with non-read statements";
            }
        }

        if (m_invalidate || (action != CACHE_IGNORE))
        {
            if (qc_query_is_type(type_mask, QUERY_TYPE_COMMIT))
            {
                m_invalidate_now = m_invalidate;
            }
            else
            {
                switch (get_statement_type(pPacket))
                {
                case StatementType::SELECT:
                    if (config.selects == CACHE_SELECTS_VERIFY_CACHEABLE)
                    {
                        // Note that the type mask must be obtained a new. A few lines
                        // above we only got the transaction state related type mask.
                        type_mask = qc_get_type_mask(pPacket);

                        if (qc_query_is_type(type_mask, QUERY_TYPE_USERVAR_READ))
                        {
                            action = CACHE_IGNORE;
                            zPrimary_reason = "user variables are read";
                        }
                        else if (qc_query_is_type(type_mask, QUERY_TYPE_SYSVAR_READ))
                        {
                            action = CACHE_IGNORE;
                            zPrimary_reason = "system variables are read";
                        }
                        else if (uses_non_cacheable_function(pPacket))
                        {
                            action = CACHE_IGNORE;
                            zPrimary_reason = "uses non-cacheable function";
                        }
                        else if (uses_non_cacheable_variable(pPacket))
                        {
                            action = CACHE_IGNORE;
                            zPrimary_reason = "uses non-cacheable variable";
                        }
                    }
                    break;

                case StatementType::DUPSERT:
                    if (m_invalidate)
                    {
                        auto mariases = static_cast<MYSQL_session*>(m_pSession->protocol_data());
                        if (!protocol_data->is_trx_active() && mariases->is_autocommit)
                        {
                            m_invalidate_now = true;
                        }

                        qc_parse_result_t result = qc_parse(pPacket, QC_COLLECT_TABLES);

                        if (result == QC_QUERY_PARSED)
                        {
                            update_table_names(pPacket);
                        }
                        else
                        {
                            const char* zPrefix = "UPDATE/DELETE/INSERT statement could not be parsed.";
                            const char* zSuffix = nullptr;

                            if (m_sCache->config().clear_cache_on_parse_errors)
                            {
                                zSuffix =
                                    "The option clear_cache_on_parse_errors is true, "
                                    "the cache will be cleared.";
                                m_clear_cache = true;
                            }
                            else
                            {
                                zSuffix =
                                    "The option clear_cache_on_parse_errors is false, "
                                    "no invalidation will take place.";
                                m_clear_cache = false;
                            }

                            if (log_decisions())
                            {
                                MXS_NOTICE("%s%s", zPrefix, zSuffix);
                            }
                        }
                    }

                /* FALLTHROUGH */
                case StatementType::UNKNOWN:
                    // A bit broad, as e.g. SHOW will cause the read only state to be turned
                    // off. However, during normal use this will always be an UPDATE, INSERT
                    // or DELETE. Note that 'm_is_read_only' only affects transactions that
                    // are not explicitly read-only.
                    m_is_read_only = false;

                    action = CACHE_IGNORE;
                    zPrimary_reason = "statement is not SELECT";
                }
            }
        }

        if (action == CACHE_USE_AND_POPULATE)
        {
            if (!m_use)
            {
                action = CACHE_POPULATE;
                zSecondary_reason = ", but usage disabled";
            }
            else if (!m_populate)
            {
                action = CACHE_USE;
                zSecondary_reason = ", but populating disabled";
            }
        }
        else if (action == CACHE_USE)
        {
            if (!m_use)
            {
                action = CACHE_IGNORE;
                zSecondary_reason = ", but usage disabled";
            }
        }
        else if (action == CACHE_POPULATE)
        {
            if (!m_populate)
            {
                action = CACHE_IGNORE;
                zSecondary_reason = ", but populating disabled";
            }
        }

        if (log_decisions())
        {
            char* pSql;
            int length;
            const int max_length = 40;

            // At this point we know it's a COM_QUERY and that the buffer is contiguous
            modutil_extract_SQL(pPacket, &pSql, &length);

            const char* zFormat;

            if (length <= max_length)
            {
                zFormat = "%s, \"%.*s\", %s%s.";
            }
            else
            {
                zFormat = "%s, \"%.*s...\", %s%s.";
                length = max_length - 3;    // strlen("...");
            }

            const char* zDecision = (action == CACHE_IGNORE) ? "IGNORE" : "CONSULT";

            mxb_assert(zPrimary_reason);
            MXS_NOTICE(zFormat, zDecision, length, pSql, zPrimary_reason, zSecondary_reason);
        }
    }
    else
    {
        if (log_decisions())
        {
            MXS_NOTICE("IGNORE: Both 'use' and 'populate' are disabled.");
        }
    }

    return action;
}

void CacheFilterSession::update_table_names(GWBUF* pPacket)
{
    mxb_assert(m_tables.empty());

    const bool fullnames = true;
    std::vector<std::string> tables = qc_get_table_names(pPacket, fullnames);

    for (auto& table : tables)
    {
        size_t dot = table.find('.');
        if (dot == std::string::npos)
        {
            if (m_zDefaultDb)
            {
                table = std::string(m_zDefaultDb) + "." + table;
            }
            else
            {
                // Without a default DB and with a non-qualified table name,
                // the query will fail, so we just ignore the table.
                continue;
            }
        }

        m_tables.insert(table);
    }
}

/**
 * Routes a COM_QUERY packet.
 *
 * @param pPacket  A contiguous COM_QUERY packet.
 *
 * @return ROUTING_ABORT if the processing of the packet should be aborted
 *         (as the data is obtained from the cache) or
 *         ROUTING_CONTINUE if the normal processing should continue.
 */
CacheFilterSession::routing_action_t CacheFilterSession::route_COM_QUERY(GWBUF* pPacket)
{
    MXB_AT_DEBUG(uint8_t * pData = static_cast<uint8_t*>(GWBUF_DATA(pPacket)));
    mxb_assert((int)MYSQL_GET_COMMAND(pData) == MXS_COM_QUERY);

    routing_action_t routing_action = ROUTING_CONTINUE;
    cache_action_t cache_action = get_cache_action(pPacket);

    if (cache_action != CACHE_IGNORE)
    {
        const CacheRules* pRules = m_sCache->should_store(m_zDefaultDb, pPacket);

        if (pRules)
        {
            static const std::string empty;

            const std::string& user = m_user_specific ? m_pSession->user() : empty;
            const std::string& host = m_user_specific ? m_pSession->client_remote() : empty;

            cache_result_t result = m_sCache->get_key(user, host, m_zDefaultDb, pPacket, &m_key);

            if (CACHE_RESULT_IS_OK(result))
            {
                routing_action = route_SELECT(cache_action, *pRules, pPacket);
            }
            else
            {
                MXS_ERROR("Could not create cache key.");
                m_state = CACHE_IGNORING_RESPONSE;
            }
        }
        else
        {
            m_state = CACHE_IGNORING_RESPONSE;
        }
    }

    return routing_action;
}


/**
 * Routes a SELECT packet.
 *
 * @param cache_action  The desired action.
 * @param rules         The current rules.
 * @param pPacket       A contiguous COM_QUERY packet containing a SELECT.
 *
 * @return ROUTING_ABORT if the processing of the packet should be aborted
 *         (as the data is obtained from the cache) or
 *         ROUTING_CONTINUE if the normal processing should continue.
 */
CacheFilterSession::routing_action_t CacheFilterSession::route_SELECT(cache_action_t cache_action,
                                                                      const CacheRules& rules,
                                                                      GWBUF* pPacket)
{
    routing_action_t routing_action = ROUTING_CONTINUE;

    if (should_use(cache_action) && rules.should_use(m_pSession))
    {
        std::weak_ptr<CacheFilterSession> sWeak {m_sThis};

        auto cb = [sWeak, pPacket](cache_result_t result, GWBUF* pResponse) {
                std::shared_ptr<CacheFilterSession> sThis = sWeak.lock();

                if (sThis)
                {
                    auto routing_action = sThis->get_value_handler(pPacket, result, pResponse);

                    if (routing_action == ROUTING_CONTINUE)
                    {
                        sThis->continue_routing(pPacket);
                    }
                    else
                    {
                        mxb_assert(pResponse);
                        // State is ROUTING_ABORT, which implies that pResponse contains the
                        // needed response. All we need to do is to send it to the client.

                        mxs::ReplyRoute down;
                        mxs::Reply reply;

                        sThis->m_up->clientReply(pResponse, down, reply);
                    }
                }
                else
                {
                    // Ok, so the session was terminated before we got a reply.
                    gwbuf_free(pPacket);
                    gwbuf_free(pResponse);
                }
            };

        uint32_t flags = CACHE_FLAGS_INCLUDE_STALE;
        GWBUF* pResponse;

        cache_result_t result = m_sCache->get_value(m_key, flags, m_soft_ttl, m_hard_ttl, &pResponse, cb);

        if (!CACHE_RESULT_IS_PENDING(result))
        {
            routing_action = get_value_handler(pPacket, result, pResponse);

            if (routing_action == ROUTING_ABORT)
            {
                // All set, arrange for the response to be delivered when
                // we return from the routeQuery() processing.
                set_response(pResponse);
            }
        }
        else
        {
            routing_action = ROUTING_ABORT;
        }
    }
    else if (should_populate(cache_action))
    {
        // We will not use any value in the cache, but we will update
        // the existing value.
        if (log_decisions())
        {
            MXS_NOTICE("Unconditionally fetching data from the server, "
                       "refreshing cache entry.");
        }
        m_state = CACHE_EXPECTING_RESPONSE;
    }
    else
    {
        // We will not use any value in the cache and we will not
        // update the existing value either.
        if (log_decisions())
        {
            MXS_NOTICE("Fetching data from server, without storing to the cache.");
        }
        m_state = CACHE_IGNORING_RESPONSE;
    }

    return routing_action;
}

namespace
{

bool get_truth_value(const char* begin, const char* end, bool* pValue)
{
    bool rv = false;

    static const char ZTRUE[] = "true";
    static const char ZFALSE[] = "false";

    static const size_t nTrue = sizeof(ZTRUE) - 1;
    static const size_t nFalse = sizeof(ZFALSE) - 1;

    size_t len = (end - begin);

    if (((len == nTrue) && (strncasecmp(begin, ZTRUE, nTrue) == 0))
        || ((len == 1) && (*begin == '1')))
    {
        *pValue = true;
        rv = true;
    }
    else if (((len == nFalse) && (strncasecmp(begin, ZFALSE, nFalse) == 0))
             || ((len == 1) && (*begin == '0')))
    {

        *pValue = false;
        rv = true;
    }

    return rv;
}

bool get_uint32_value(const char* begin, const char* end, uint32_t* pValue)
{
    bool rv = false;

    size_t len = end - begin;
    char copy[len + 1];

    memcpy(copy, begin, len);
    copy[len] = 0;

    errno = 0;
    char* p;
    long int l = strtol(copy, &p, 10);

    if ((errno == 0) && (*p == 0))
    {
        if (l >= 0)
        {
            *pValue = l;
            rv = true;
        }
    }

    return rv;
}

char* create_bool_error_message(const char* zName, const char* pValue_begin, const char* pValue_end)
{
    static const char FORMAT[] = "The variable %s can only have the values true/false/1/0";
    int n = snprintf(NULL, 0, FORMAT, zName) + 1;

    char* zMessage = static_cast<char*>(MXS_MALLOC(n));

    if (zMessage)
    {
        sprintf(zMessage, FORMAT, zName);
    }

    int len = pValue_end - pValue_begin;
    MXS_WARNING("Attempt to set the variable %s to the invalid value \"%.*s\".",
                zName,
                len,
                pValue_begin);

    return zMessage;
}

char* create_uint32_error_message(const char* zName, const char* pValue_begin, const char* pValue_end)
{
    static const char FORMAT[] = "The variable %s can have as value 0 or a positive integer.";
    int n = snprintf(NULL, 0, FORMAT, zName) + 1;

    char* zMessage = static_cast<char*>(MXS_MALLOC(n));

    if (zMessage)
    {
        sprintf(zMessage, FORMAT, zName);
    }

    int len = pValue_end - pValue_begin;
    MXS_WARNING("Attempt to set the variable %s to the invalid value \"%.*s\".",
                zName,
                len,
                pValue_begin);

    return zMessage;
}
}

char* CacheFilterSession::set_cache_populate(const char* zName,
                                             const char* pValue_begin,
                                             const char* pValue_end)
{
    mxb_assert(strcmp(SV_MAXSCALE_CACHE_POPULATE, zName) == 0);

    char* zMessage = NULL;

    bool enabled;

    if (get_truth_value(pValue_begin, pValue_end, &enabled))
    {
        m_populate = enabled;
    }
    else
    {
        zMessage = create_bool_error_message(zName, pValue_begin, pValue_end);
    }

    return zMessage;
}

char* CacheFilterSession::set_cache_use(const char* zName,
                                        const char* pValue_begin,
                                        const char* pValue_end)
{
    mxb_assert(strcmp(SV_MAXSCALE_CACHE_USE, zName) == 0);

    char* zMessage = NULL;

    bool enabled;

    if (get_truth_value(pValue_begin, pValue_end, &enabled))
    {
        m_use = enabled;
    }
    else
    {
        zMessage = create_bool_error_message(zName, pValue_begin, pValue_end);
    }

    return zMessage;
}

char* CacheFilterSession::set_cache_soft_ttl(const char* zName,
                                             const char* pValue_begin,
                                             const char* pValue_end)
{
    mxb_assert(strcmp(SV_MAXSCALE_CACHE_SOFT_TTL, zName) == 0);

    char* zMessage = NULL;

    uint32_t value;

    if (get_uint32_value(pValue_begin, pValue_end, &value))
    {
        // The config value is stored in milliseconds, but runtime changes
        // are made in seconds.
        m_soft_ttl = value * 1000;
    }
    else
    {
        zMessage = create_uint32_error_message(zName, pValue_begin, pValue_end);
    }

    return zMessage;
}

char* CacheFilterSession::set_cache_hard_ttl(const char* zName,
                                             const char* pValue_begin,
                                             const char* pValue_end)
{
    mxb_assert(strcmp(SV_MAXSCALE_CACHE_HARD_TTL, zName) == 0);

    char* zMessage = NULL;

    uint32_t value;

    if (get_uint32_value(pValue_begin, pValue_end, &value))
    {
        // The config value is stored in milliseconds, but runtime changes
        // are made in seconds.
        m_hard_ttl = value * 1000;
    }
    else
    {
        zMessage = create_uint32_error_message(zName, pValue_begin, pValue_end);
    }

    return zMessage;
}

// static
char* CacheFilterSession::set_cache_populate(void* pContext,
                                             const char* zName,
                                             const char* pValue_begin,
                                             const char* pValue_end)
{
    CacheFilterSession* pThis = static_cast<CacheFilterSession*>(pContext);

    return pThis->set_cache_populate(zName, pValue_begin, pValue_end);
}

// static
char* CacheFilterSession::set_cache_use(void* pContext,
                                        const char* zName,
                                        const char* pValue_begin,
                                        const char* pValue_end)
{
    CacheFilterSession* pThis = static_cast<CacheFilterSession*>(pContext);

    return pThis->set_cache_use(zName, pValue_begin, pValue_end);
}

// static
char* CacheFilterSession::set_cache_soft_ttl(void* pContext,
                                             const char* zName,
                                             const char* pValue_begin,
                                             const char* pValue_end)
{
    CacheFilterSession* pThis = static_cast<CacheFilterSession*>(pContext);

    return pThis->set_cache_soft_ttl(zName, pValue_begin, pValue_end);
}

// static
char* CacheFilterSession::set_cache_hard_ttl(void* pContext,
                                             const char* zName,
                                             const char* pValue_begin,
                                             const char* pValue_end)
{
    CacheFilterSession* pThis = static_cast<CacheFilterSession*>(pContext);

    return pThis->set_cache_hard_ttl(zName, pValue_begin, pValue_end);
}

bool CacheFilterSession::put_value_handler(cache_result_t result,
                                           const mxs::ReplyRoute& down,
                                           const mxs::Reply& reply)
{
    bool rv = true;

    if (CACHE_RESULT_IS_OK(result))
    {
        prepare_response();
    }
    else
    {
        MXS_ERROR("Could not store new cache value, deleting a possibly existing old value.");

        std::weak_ptr<CacheFilterSession> sWeak {m_sThis};

        result = m_sCache->del_value(m_key,
                                     [sWeak, down, reply](cache_result_t result) {
                                         auto sThis = sWeak.lock();

                                        // If we do not have an sThis, then the session
                                        // has been terminated.
                                         if (sThis)
                                         {
                                             sThis->del_value_handler(result);
                                             sThis->flush_response(down, reply);
                                         }
                                     });

        if (CACHE_RESULT_IS_PENDING(result))
        {
            rv = false;
        }
        else
        {
            del_value_handler(result);
        }
    }

    return rv;
}

void CacheFilterSession::del_value_handler(cache_result_t result)
{
    if (!(CACHE_RESULT_IS_OK(result) || CACHE_RESULT_IS_NOT_FOUND(result)))
    {
        MXS_ERROR("Could not delete cache item, the value may now be stale.");
    }

    prepare_response();
}

CacheFilterSession::routing_action_t CacheFilterSession::get_value_handler(GWBUF* pPacket,
                                                                           cache_result_t result,
                                                                           GWBUF* pResponse)
{
    routing_action_t routing_action = ROUTING_CONTINUE;

    if (CACHE_RESULT_IS_OK(result))
    {
        if (CACHE_RESULT_IS_STALE(result))
        {
            // The value was found, but it was stale. Now we need to
            // figure out whether somebody else is already fetching it.

            if (m_sCache->must_refresh(m_key, this))
            {
                // We were the first ones who hit the stale item. It's
                // our responsibility now to fetch it.
                if (log_decisions())
                {
                    MXS_NOTICE("Cache data is stale, fetching fresh from server.");
                }

                // As we don't use the response it must be freed.
                gwbuf_free(pResponse);

                m_refreshing = true;
                routing_action = ROUTING_CONTINUE;
            }
            else
            {
                // Somebody is already fetching the new value. So, let's
                // use the stale value. No point in hitting the server twice.
                if (log_decisions())
                {
                    MXS_NOTICE("Cache data is stale but returning it, fresh "
                               "data is being fetched already.");
                }
                routing_action = ROUTING_ABORT;
            }
        }
        else
        {
            if (log_decisions())
            {
                MXS_NOTICE("Using fresh data from cache.");
            }
            routing_action = ROUTING_ABORT;
        }
    }
    else
    {
        if (log_decisions())
        {
            MXS_NOTICE("Not found in cache, fetching data from server.");
        }
        routing_action = ROUTING_CONTINUE;
    }

    if (routing_action == ROUTING_CONTINUE)
    {
        // If we are populating or refreshing, or the result was discarded
        // due to hard TTL having kicked in, then we fetch the result *and*
        // update the cache. That is, as long as there is room in the cache
        // an entry will stay there.
        if (m_populate || m_refreshing || CACHE_RESULT_IS_DISCARDED(result))
        {
            m_state = CACHE_EXPECTING_RESPONSE;
        }
        else
        {
            if (log_decisions())
            {
                MXS_NOTICE("Neither populating, nor refreshing, fetching data "
                           "but not adding to cache.");
            }
            m_state = CACHE_IGNORING_RESPONSE;
        }
    }
    else
    {
        if (log_decisions())
        {
            MXS_NOTICE("Found in cache.");
        }

        m_state = CACHE_EXPECTING_NOTHING;
        gwbuf_free(pPacket);
    }

    return routing_action;
}

int CacheFilterSession::continue_routing(GWBUF* pPacket)
{
    if (m_invalidate && m_state == CACHE_EXPECTING_RESPONSE)
    {
        qc_parse_result_t parse_result = qc_parse(pPacket, QC_COLLECT_TABLES);

        if (parse_result == QC_QUERY_PARSED)
        {
            update_table_names(pPacket);
        }
        else
        {
            MXS_WARNING("Invalidation is enabled but the current statement could not "
                        "be parsed. Consequently, the result cannot be cached.");
            m_state = CACHE_IGNORING_RESPONSE;
        }
    }

    return FilterSession::routeQuery(pPacket);
}
