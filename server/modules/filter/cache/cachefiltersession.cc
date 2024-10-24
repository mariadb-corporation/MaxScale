/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#define MXB_MODULE_NAME "cache"
#include "cachefiltersession.hh"
#include <new>
#include <utility>
#include <maxbase/alloc.hh>
#include <maxbase/pretty_print.hh>
#include <maxscale/parser.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include <maxsimd/multistmt.hh>
#include "storage.hh"

using mxb::Worker;
using mxs::Parser;

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

inline bool uses_name(std::string_view name, const char** pzNames, size_t nNames)
{
    // TODO: Turn the char* array into a std::string_view map.
    return bsearch(std::string(name).c_str(), pzNames, nNames, sizeof(const char*), compare_name) != NULL;
}

bool uses_non_cacheable_function(const Parser& parser, const GWBUF& packet)
{
    bool rv = false;

    const Parser::FunctionInfo* pInfo;
    size_t nInfos;

    parser.get_function_info(packet, &pInfo, &nInfos);

    const Parser::FunctionInfo* pEnd = pInfo + nInfos;

    while (!rv && (pInfo != pEnd))
    {
        rv = uses_name(pInfo->name, NON_CACHEABLE_FUNCTIONS, N_NON_CACHEABLE_FUNCTIONS);

        ++pInfo;
    }

    return rv;
}

bool uses_non_cacheable_variable(const Parser& parser, const GWBUF& packet)
{
    bool rv = false;

    const Parser::FieldInfo* pInfo;
    size_t nInfos;

    parser.get_field_info(packet, &pInfo, &nInfos);

    const Parser::FieldInfo* pEnd = pInfo + nInfos;

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
    DRALTER,    // DROP, RENAME, ALTER
    UNKNOWN
};

StatementType get_statement_type(std::string_view sql)
{
    StatementType type = StatementType::UNKNOWN;

    const char* pSql = sql.data();
    int len = sql.length();

    const char* pSql_end = pSql + len;

    pSql = mariadb::bypass_whitespace(pSql, len);

    static const char ALTER[]  = "ALTER";
    static const char DELETE[] = "DELETE";
    static const char DROP[]   = "DROP";
    static const char INSERT[] = "INSERT";
    static const char RENAME[] = "RENAME";
    static const char SELECT[] = "SELECT";
    static const char UPDATE[] = "UPDATE";

    const char* pKey = nullptr;
    const char* pKey_end = nullptr;

    if (pSql_end > pSql)
    {
        switch (*pSql)
        {
        case 'A':
        case 'a':
            type = StatementType::DRALTER;
            pKey = ALTER;
            pKey_end = pKey + sizeof(ALTER) - 1;
            break;

        case 'D':
        case 'd':
            if (pSql_end > pSql + 1)
            {
                switch (*(pSql + 1))
                {
                case 'r':
                case 'R':
                    type = StatementType::DRALTER;
                    pKey = DROP;
                    pKey_end = pKey + sizeof(DROP) - 1;
                    break;

                case 'e':
                case 'E':
                    type = StatementType::DUPSERT;
                    pKey = DELETE;
                    pKey_end = pKey + sizeof(DELETE) - 1;
                    break;
                }
            }
            break;

        case 'I':
        case 'i':
            type = StatementType::DUPSERT;
            pKey = INSERT;
            pKey_end = pKey + sizeof(INSERT) - 1;
            break;

        case 'R':
        case 'r':
            type = StatementType::DRALTER;
            pKey = RENAME;
            pKey_end = pKey + sizeof(RENAME) - 1;
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
                // the statement is erroneous) or the keyword was followed by something else
                // than an alphanumeric character, e.g. whitespace.
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
    , m_processing(false)
{
    m_key.data_hash = 0;
    m_key.full_hash = 0;

    reset_response_state();

    static bool warn_about_variables = true;
    int msg_level = warn_about_variables ? LOG_WARNING : LOG_INFO;
    bool warned = false;

    if (!pSession->add_variable(SV_MAXSCALE_CACHE_POPULATE, &CacheFilterSession::set_cache_populate, this))
    {
        MXB_LOG_MESSAGE(msg_level,
                        "Could not add MaxScale user variable '%s', dynamically "
                        "enabling/disabling the populating of the cache is not possible for this filter.",
                        SV_MAXSCALE_CACHE_POPULATE);
        warned = true;
    }

    if (!pSession->add_variable(SV_MAXSCALE_CACHE_USE, &CacheFilterSession::set_cache_use, this))
    {
        MXB_LOG_MESSAGE(msg_level,
                        "Could not add MaxScale user variable '%s', dynamically "
                        "enabling/disabling the using of the cache not possible for this filter.",
                        SV_MAXSCALE_CACHE_USE);
        warned = true;
    }

    if (!pSession->add_variable(SV_MAXSCALE_CACHE_SOFT_TTL, &CacheFilterSession::set_cache_soft_ttl, this))
    {
        MXB_LOG_MESSAGE(msg_level,
                        "Could not add MaxScale user variable '%s', dynamically "
                        "setting the soft TTL not possible for this filter.",
                        SV_MAXSCALE_CACHE_SOFT_TTL);
        warned = true;
    }

    if (!pSession->add_variable(SV_MAXSCALE_CACHE_HARD_TTL, &CacheFilterSession::set_cache_hard_ttl, this))
    {
        MXB_LOG_MESSAGE(msg_level,
                        "Could not add MaxScale user variable '%s', dynamically "
                        "setting the hard TTL not possible for this filter.",
                        SV_MAXSCALE_CACHE_HARD_TTL);
        warned = true;
    }

    if (warned)
    {
        warn_about_variables = false;
    }
}

CacheFilterSession::~CacheFilterSession()
{
    MXB_FREE(m_zUseDb);
    MXB_FREE(m_zDefaultDb);
}

namespace
{

const std::string empty_string;

}

const std::string& CacheFilterSession::user() const
{
    return m_user_specific ? m_pSession->user() : empty_string;
}

const std::string& CacheFilterSession::host() const
{
    return m_user_specific ? m_pSession->client_remote() : empty_string;
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
        zDefaultDb = MXB_STRDUP(db.c_str());
    }

    if (db.empty() || zDefaultDb)
    {
        pCacheFilterSession = new(std::nothrow) CacheFilterSession(pSession,
                                                                   pService,
                                                                   std::move(sCache),
                                                                   zDefaultDb);

        if (!pCacheFilterSession)
        {
            MXB_FREE(zDefaultDb);
        }
    }

    return pCacheFilterSession;
}

bool CacheFilterSession::routeQuery(GWBUF&& packet)
{
    int rv = 1;

    if (m_processing)
    {
        if (MYSQL_GET_PACKET_NO(packet.data()) == 0)
        {
            // A new protocol command, queue it.
            m_queued_packets.push_back(std::move(packet));
        }
        else
        {
            // A subsequent packet of a multi-packet protocol command, just send forward.
            rv = FilterSession::routeQuery(std::move(packet));
        }
    }
    else
    {
        routing_action_t action = ROUTING_CONTINUE;

        reset_response_state();
        m_state = CACHE_IGNORING_RESPONSE;

        if (!m_load_active)
        {
            m_processing = true;

            // The following is necessary for the case that the delayed call
            // made in read_for_another_call() arrives *after* a routeQuery()
            // call made due to the client having sent more data. With this
            // it is ensured that the packets are handled in the right order.
            if (!m_queued_packets.empty())
            {
                m_queued_packets.push_back(std::move(packet));
                packet = std::move(m_queued_packets.front());
                m_queued_packets.pop_front();
            }

            uint8_t* pData = packet.data();

            // All of these should be guaranteed by RCAP_TYPE_TRANSACTION_TRACKING
            mxb_assert(packet.length() >= MYSQL_HEADER_LEN + 1);
            mxb_assert(MYSQL_GET_PAYLOAD_LEN(pData) + MYSQL_HEADER_LEN == packet.length());

            switch (mariadb::get_command(pData))
            {
            case MXS_COM_INIT_DB:
                {
                    mxb_assert(!m_zUseDb);
                    size_t len = MYSQL_GET_PAYLOAD_LEN(pData) - 1;      // Remove the command byte.
                    m_zUseDb = (char*)MXB_MALLOC(len + 1);

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
                    MXB_NOTICE("COM_STMT_PREPARE, ignoring.");
                }
                break;

            case MXS_COM_STMT_EXECUTE:
                if (log_decisions())
                {
                    MXB_NOTICE("COM_STMT_EXECUTE, ignoring.");
                }
                break;

            case MXS_COM_QUERY:
                {
                    if (!maxsimd::is_multi_stmt(get_sql_string(packet)))
                    {
                        action = route_COM_QUERY(packet.shallow_clone());
                    }
                    else if (log_decisions())
                    {
                        MXB_NOTICE("Multi-statement, ignoring.");
                    }
                }
                break;

            default:
                break;
            }
        }

        if (action == ROUTING_CONTINUE)
        {
            rv = continue_routing(std::move(packet));
        }
    }

    return rv;
}

int CacheFilterSession::client_reply_post_process(const mxs::ReplyRoute& down,
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
        MXB_ERROR("Internal cache logic broken, unexpected state: %d", m_state);
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
        MXB_ERROR("Could not clear the cache, which is now in "
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
            MXB_NOTICE("Cache successfully invalidated.");
        }
    }
    else
    {
        MXB_WARNING("Failed to invalidate individual cache entries, "
                    "the cache will now be cleared.");
        clear_cache();
    }
}

bool CacheFilterSession::clientReply(GWBUF&& data, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    if (m_res)
    {
        m_res.append(data);
    }
    else
    {
        m_res = std::move(data);
    }

    if (reply.state() == mxs::ReplyState::LOAD_DATA)
    {
        m_load_active = true;
    }
    else if (m_load_active && reply.is_complete())
    {
        m_load_active = false;
    }

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
        // autocommit being true.

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
                                             [sWeak, down, reply](cache_result_t res) {
                        std::shared_ptr<CacheFilterSession> sThis = sWeak.lock();

                        if (sThis)
                        {
                            sThis->invalidate_handler(res);

                            sThis->client_reply_post_process(down, reply);
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
        rv = client_reply_post_process(down, reply);
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
        MXB_INFO("Error packet received from backend: %s", reply.error().message().c_str());
    }
    else
    {
        MXB_WARNING("Received data from the backend although filter is expecting nothing.");
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
        MXB_FREE(m_zUseDb);
        m_zUseDb = NULL;
    }
    else
    {
        mxb_assert(mariadb::get_command(m_res) == MYSQL_REPLY_OK);
        // In case m_zUseDb could not be allocated in routeQuery(), we will
        // in fact reset the default db here. That's ok as it will prevent broken
        // entries in the cache.
        MXB_FREE(m_zDefaultDb);
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
            MXB_NOTICE("Current resultset size exceeds maximum allowed size %s. Not caching.",
                       mxb::pretty_size(m_sCache->config().max_resultset_size).c_str());
        }

        prepare_response();
        m_state = CACHE_IGNORING_RESPONSE;
    }
    else if (cache_max_resultset_rows_exceeded(m_sCache->config(), reply.rows_read()))
    {
        if (log_decisions())
        {
            MXB_NOTICE("Max rows %lu reached, not caching result.", reply.rows_read());
        }

        prepare_response();
        m_state = CACHE_IGNORING_RESPONSE;
    }
    else if (reply.is_complete())
    {
        if (log_decisions())
        {
            MXB_NOTICE("Result collected, rows: %lu, size: %s", reply.rows_read(),
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
    m_next_response = std::exchange(m_res, GWBUF());
}

/**
 * Sends data to the client, if there is something to send.
 */
int CacheFilterSession::flush_response(const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    GWBUF next_response = std::exchange(m_next_response, GWBUF());
    int rv = 1;

    if (next_response)
    {
        rv = FilterSession::clientReply(std::move(next_response), down, reply);
        ready_for_another_call();
    }

    return rv;
}

/**
 * Reset cache response state
 */
void CacheFilterSession::reset_response_state()
{
    m_res.clear();
}

/**
 * Store the data.
 */
void CacheFilterSession::store_and_prepare_response(const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxb_assert(m_res);
    std::vector<std::string> invalidation_words;
    bool put_value = true;

    if (m_invalidate)
    {
        for (const auto& table : m_tables)
        {
            if (table.find("information_schema.") == 0)
            {
                // If any table from "information_schema" is involved in the SELECT,
                // the result will not be cached.
                invalidation_words.clear();
                put_value = false;
                break;
            }
            else
            {
                invalidation_words.emplace_back(table);
            }
        }

        m_tables.clear();
    }

    cache_result_t result = CACHE_RESULT_OK;

    if (put_value)
    {
        std::weak_ptr<CacheFilterSession> sWeak {m_sThis};

        result = m_sCache->put_value(m_key, invalidation_words, m_res,
                                     [sWeak, down, reply](cache_result_t res) {
                                         auto sThis = sWeak.lock();

                                         // If we do not have an sThis, then the session
                                         // has been terminated.
                                         if (sThis)
                                         {
                                             if (sThis->put_value_handler(res, down, reply))
                                             {
                                                 sThis->flush_response(down, reply);
                                             }
                                         }
                                     });
    }

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
CacheFilterSession::cache_action_t CacheFilterSession::get_cache_action(const GWBUF& packet)
{
    cache_action_t action = CACHE_IGNORE;

    m_invalidate_now = false;

    if (m_use || m_populate)
    {
        uint32_t type_mask = parser().get_trx_type_mask(packet); // Note, only trx-related type mask

        const char* zPrimary_reason = NULL;
        const char* zSecondary_reason = "";
        const CacheConfig& config = m_sCache->config();
        auto* pProtocol_data = m_pSession->protocol_data();

        if (Parser::type_mask_contains(type_mask, mxs::sql::TYPE_BEGIN_TRX))
        {
            if (log_decisions())
            {
                zPrimary_reason = "transaction start";
            }

            // When a transaction is started, we initially assume it is read-only.
            m_is_read_only = true;
        }
        else if (!pProtocol_data->is_trx_active())
        {
            if (log_decisions())
            {
                zPrimary_reason = "no transaction";
            }
            action = CACHE_USE_AND_POPULATE;
        }
        else if (pProtocol_data->is_trx_read_only())
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
            if (Parser::type_mask_contains(type_mask, mxs::sql::TYPE_COMMIT))
            {
                m_invalidate_now = m_invalidate;
            }
            else
            {
                auto statement_type = get_statement_type(parser().get_sql(packet));

                switch (statement_type)
                {
                case StatementType::SELECT:
                    if (config.selects == CACHE_SELECTS_VERIFY_CACHEABLE)
                    {
                        // Note that the type mask must be obtained a new. A few lines
                        // above we only got the transaction state related type mask.
                        type_mask = parser().get_type_mask(packet);

                        if (Parser::type_mask_contains(type_mask, mxs::sql::TYPE_USERVAR_READ))
                        {
                            action = CACHE_IGNORE;
                            zPrimary_reason = "user variables are read";
                        }
                        else if (Parser::type_mask_contains(type_mask, mxs::sql::TYPE_SYSVAR_READ))
                        {
                            action = CACHE_IGNORE;
                            zPrimary_reason = "system variables are read";
                        }
                        else if (uses_non_cacheable_function(parser(), packet))
                        {
                            action = CACHE_IGNORE;
                            zPrimary_reason = "uses non-cacheable function";
                        }
                        else if (uses_non_cacheable_variable(parser(), packet))
                        {
                            action = CACHE_IGNORE;
                            zPrimary_reason = "uses non-cacheable variable";
                        }
                    }
                    break;

                case StatementType::DRALTER:
                case StatementType::DUPSERT:
                    if (m_invalidate)
                    {
                        if (statement_type == StatementType::DRALTER)
                        {
                            m_invalidate_now = true;
                        }
                        if (!pProtocol_data->is_trx_active() && pProtocol_data->is_autocommit())
                        {
                            m_invalidate_now = true;
                        }

                        Parser::Result result = parser().parse(packet, Parser::COLLECT_TABLES);

                        if (result == Parser::Result::PARSED)
                        {
                            update_table_names(packet);
                        }
                        else
                        {
                            const char* zPrefix = "Modifying statement could not be parsed. ";
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
                                MXB_NOTICE("%s%s", zPrefix, zSuffix);
                            }
                        }
                    }

                [[fallthrough]];
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
            // At this point we know it's a query.
            std::string_view sql = parser().get_sql(packet);
            const char* pSql = sql.data();
            int length = sql.length();
            const int max_length = 40;

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
            MXB_NOTICE(zFormat, zDecision, length, pSql, zPrimary_reason, zSecondary_reason);
        }
    }
    else
    {
        if (log_decisions())
        {
            MXB_NOTICE("IGNORE: Both 'use' and 'populate' are disabled.");
        }
    }

    return action;
}

void CacheFilterSession::update_table_names(const GWBUF& packet)
{
    // In case of BEGIN INSERT ...; INSERT ...; COMMIT m_tables may already contain data.

    std::vector<mxs::Parser::TableName> names = parser().get_table_names(packet);

    for (auto& name : names)
    {
        std::string qtable;

        if (name.db.empty())
        {
            if (m_zDefaultDb)
            {
                qtable = std::string(m_zDefaultDb) + "." + std::string(name.table);
            }
            else
            {
                // Without a default DB and with a non-qualified table name,
                // the query will fail, so we just ignore the table.
                continue;
            }
        }
        else
        {
            qtable = name.db;
            qtable += '.';
            qtable += name.table;
        }

        m_tables.insert(qtable);
    }
}

/**
 * Routes a COM_QUERY packet.
 *
 * @param packet  A contiguous COM_QUERY packet.
 *
 * @return ROUTING_ABORT if the processing of the packet should be aborted
 *         (as the data is obtained from the cache) or
 *         ROUTING_CONTINUE if the normal processing should continue.
 */
CacheFilterSession::routing_action_t CacheFilterSession::route_COM_QUERY(GWBUF&& packet)
{
    MXB_AT_DEBUG(uint8_t * pData = packet.data());
    mxb_assert(mariadb::get_command(pData) == MXS_COM_QUERY);

    routing_action_t routing_action = ROUTING_CONTINUE;
    cache_action_t cache_action = get_cache_action(packet);

    if (cache_action != CACHE_IGNORE)
    {
        std::shared_ptr<CacheRules> sRules = m_sCache->should_store(parser(), m_zDefaultDb, packet);

        if (sRules)
        {
            cache_result_t result = m_sCache->get_key(user(), host(), m_zDefaultDb, packet, &m_key);

            if (CACHE_RESULT_IS_OK(result))
            {
                routing_action = route_SELECT(cache_action, *sRules.get(), std::move(packet));
            }
            else
            {
                MXB_ERROR("Could not create cache key.");
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
 * @param packet        A contiguous COM_QUERY packet containing a SELECT.
 *
 * @return ROUTING_ABORT if the processing of the packet should be aborted
 *         (as the data is obtained from the cache) or
 *         ROUTING_CONTINUE if the normal processing should continue.
 */
CacheFilterSession::routing_action_t CacheFilterSession::route_SELECT(cache_action_t cache_action,
                                                                      const CacheRules& rules,
                                                                      GWBUF&& packet)
{
    routing_action_t routing_action = ROUTING_CONTINUE;

    if (should_use(cache_action) && rules.should_use(m_pSession))
    {
        std::weak_ptr<CacheFilterSession> sWeak {m_sThis};

        auto cb = [sWeak, sBuffer = std::make_shared<GWBUF>(std::move(packet))](cache_result_t result, GWBUF&& response) {
                std::shared_ptr<CacheFilterSession> sThis = sWeak.lock();

                if (sThis)
                {
                    auto action = sThis->get_value_handler(result);

                    if (action == ROUTING_CONTINUE)
                    {
                        sThis->continue_routing(std::move(*sBuffer));
                    }
                    else
                    {
                        mxb_assert(!response.empty());
                        // State is ROUTING_ABORT, which implies that pResponse contains the
                        // needed response. All we need to do is to send it to the client.

                        mxs::ReplyRoute down;
                        mxs::Reply reply = sThis->protocol().make_reply(response);

                        sThis->m_up->clientReply(std::move(response), down, reply);
                        sThis->ready_for_another_call();
                    }
                }
            };

        uint32_t flags = CACHE_FLAGS_INCLUDE_STALE;
        GWBUF response;

        cache_result_t result = m_sCache->get_value(m_key, flags, m_soft_ttl, m_hard_ttl, &response, cb);

        if (!CACHE_RESULT_IS_PENDING(result))
        {
            routing_action = get_value_handler(result);

            if (routing_action == ROUTING_ABORT)
            {
                // All set, arrange for the response to be delivered when
                // we return from the routeQuery() processing.
                set_response(std::move(response));
                ready_for_another_call();
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
            MXB_NOTICE("Unconditionally fetching data from the server, "
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
            MXB_NOTICE("Fetching data from server, without storing to the cache.");
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

    char* zMessage = static_cast<char*>(MXB_MALLOC(n));

    if (zMessage)
    {
        sprintf(zMessage, FORMAT, zName);
    }

    int len = pValue_end - pValue_begin;
    MXB_WARNING("Attempt to set the variable %s to the invalid value \"%.*s\".",
                zName,
                len,
                pValue_begin);

    return zMessage;
}

char* create_uint32_error_message(const char* zName, const char* pValue_begin, const char* pValue_end)
{
    static const char FORMAT[] = "The variable %s can have as value 0 or a positive integer.";
    int n = snprintf(NULL, 0, FORMAT, zName) + 1;

    char* zMessage = static_cast<char*>(MXB_MALLOC(n));

    if (zMessage)
    {
        sprintf(zMessage, FORMAT, zName);
    }

    int len = pValue_end - pValue_begin;
    MXB_WARNING("Attempt to set the variable %s to the invalid value \"%.*s\".",
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
        MXB_ERROR("Could not store new cache value, deleting a possibly existing old value.");

        std::weak_ptr<CacheFilterSession> sWeak {m_sThis};

        result = m_sCache->del_value(m_key,
                                     [sWeak, down, reply](cache_result_t res) {
                                         auto sThis = sWeak.lock();

                                        // If we do not have an sThis, then the session
                                        // has been terminated.
                                         if (sThis)
                                         {
                                             sThis->del_value_handler(res);
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
        MXB_ERROR("Could not delete cache item, the value may now be stale.");
    }

    prepare_response();
}

CacheFilterSession::routing_action_t CacheFilterSession::get_value_handler(cache_result_t result)
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
                    MXB_NOTICE("Cache data is stale, fetching fresh from server.");
                }

                m_refreshing = true;
                routing_action = ROUTING_CONTINUE;
            }
            else
            {
                // Somebody is already fetching the new value. So, let's
                // use the stale value. No point in hitting the server twice.
                if (log_decisions())
                {
                    MXB_NOTICE("Cache data is stale but returning it, fresh "
                               "data is being fetched already.");
                }
                routing_action = ROUTING_ABORT;
            }
        }
        else
        {
            if (log_decisions())
            {
                MXB_NOTICE("Using fresh data from cache.");
            }
            routing_action = ROUTING_ABORT;
        }
    }
    else
    {
        if (log_decisions())
        {
            MXB_NOTICE("Not found in cache, fetching data from server.");
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
                MXB_NOTICE("Neither populating, nor refreshing, fetching data "
                           "but not adding to cache.");
            }
            m_state = CACHE_IGNORING_RESPONSE;
        }
    }
    else
    {
        if (log_decisions())
        {
            MXB_NOTICE("Found in cache.");
        }

        m_state = CACHE_EXPECTING_NOTHING;
    }

    return routing_action;
}

int CacheFilterSession::continue_routing(GWBUF&& packet)
{
    if (m_invalidate && m_state == CACHE_EXPECTING_RESPONSE)
    {
        Parser::Result parse_result = parser().parse(packet, Parser::COLLECT_TABLES);

        if (parse_result == Parser::Result::PARSED)
        {
            update_table_names(packet);
        }
        else
        {
            std::string_view sql = parser().get_sql(packet);
            const char* pSql = sql.data();
            int len = sql.length();

            MXB_INFO("Invalidation is enabled, but the current statement could not "
                     "be parsed. Consequently the accessed tables are not known and "
                     "hence the result cannot be cached, as it would not be known when "
                     "the result should be invalidated, stmt: %.*s", len, pSql);
            m_state = CACHE_IGNORING_RESPONSE;
        }
    }

    if (!protocol_data().will_respond(packet))
    {
        m_processing = false;
    }

    return FilterSession::routeQuery(std::move(packet));
}

void CacheFilterSession::ready_for_another_call()
{
    m_processing = false;

    if (!m_queued_packets.empty())
    {
        m_pSession->delay_routing(this, GWBUF {}, 0ms, [this](GWBUF&& unused) {
            bool ok = true;
            // We may already be processing, if a packet arrived from the client
            // and processed, before the delayed call got handled.
            if (!m_processing)
            {
                if (!m_queued_packets.empty())
                {
                    GWBUF packet = std::move(m_queued_packets.front());
                    m_queued_packets.pop_front();

                    ok = routeQuery(std::move(packet));
                }
            }

            return ok;
        });
    }
}
