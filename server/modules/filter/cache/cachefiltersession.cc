/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "cachefiltersession.hh"
#include <new>
#include <maxscale/alloc.h>
#include <maxscale/query_classifier.h>
#include <maxscale/mysql_utils.h>
#include "storage.hh"

namespace
{

inline bool cache_max_resultset_rows_exceeded(const CACHE_CONFIG& config, uint64_t rows)
{
    return config.max_resultset_rows == 0 ? false : rows > config.max_resultset_rows;
}

inline bool cache_max_resultset_size_exceeded(const CACHE_CONFIG& config, uint64_t size)
{
    return config.max_resultset_size == 0 ? false : size > config.max_resultset_size;
}

}

CacheFilterSession::CacheFilterSession(MXS_SESSION* pSession, Cache* pCache, char* zDefaultDb)
    : maxscale::FilterSession(pSession)
    , m_state(CACHE_EXPECTING_NOTHING)
    , m_pCache(pCache)
    , m_zDefaultDb(zDefaultDb)
    , m_zUseDb(NULL)
    , m_refreshing(false)
    , m_is_read_only(true)
{
    memset(m_key.data, 0, CACHE_KEY_MAXLEN);

    reset_response_state();
}

CacheFilterSession::~CacheFilterSession()
{
    MXS_FREE(m_zUseDb);
    MXS_FREE(m_zDefaultDb);
}

//static
CacheFilterSession* CacheFilterSession::Create(Cache* pCache, MXS_SESSION* pSession)
{
    CacheFilterSession* pCacheFilterSession = NULL;

    ss_dassert(pSession->client_dcb);
    ss_dassert(pSession->client_dcb->data);

    MYSQL_session *pMysqlSession = (MYSQL_session*)pSession->client_dcb->data;
    char* zDefaultDb = NULL;

    if (pMysqlSession->db[0] != 0)
    {
        zDefaultDb = MXS_STRDUP(pMysqlSession->db);
    }

    if ((pMysqlSession->db[0] == 0) || zDefaultDb)
    {
        pCacheFilterSession = new (std::nothrow) CacheFilterSession(pSession, pCache, zDefaultDb);

        if (!pCacheFilterSession)
        {
            MXS_FREE(zDefaultDb);
        }
    }

    return pCacheFilterSession;
}

void CacheFilterSession::close()
{
}

int CacheFilterSession::routeQuery(GWBUF* pPacket)
{
    uint8_t* pData = static_cast<uint8_t*>(GWBUF_DATA(pPacket));

    // All of these should be guaranteed by RCAP_TYPE_TRANSACTION_TRACKING
    ss_dassert(GWBUF_IS_CONTIGUOUS(pPacket));
    ss_dassert(GWBUF_LENGTH(pPacket) >= MYSQL_HEADER_LEN + 1);
    ss_dassert(MYSQL_GET_PAYLOAD_LEN(pData) + MYSQL_HEADER_LEN == GWBUF_LENGTH(pPacket));

    bool fetch_from_server = true;

    reset_response_state();
    m_state = CACHE_IGNORING_RESPONSE;

    int rv;

    switch ((int)MYSQL_GET_COMMAND(pData))
    {
    case MYSQL_COM_INIT_DB:
        {
            ss_dassert(!m_zUseDb);
            size_t len = MYSQL_GET_PAYLOAD_LEN(pData) - 1; // Remove the command byte.
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

    case MYSQL_COM_QUERY:
        if (should_consult_cache(pPacket))
        {
            if (m_pCache->should_store(m_zDefaultDb, pPacket))
            {
                if (m_pCache->should_use(m_pSession))
                {
                    GWBUF* pResponse;
                    cache_result_t result = get_cached_response(pPacket, &pResponse);

                    if (CACHE_RESULT_IS_OK(result))
                    {
                        if (CACHE_RESULT_IS_STALE(result))
                        {
                            // The value was found, but it was stale. Now we need to
                            // figure out whether somebody else is already fetching it.

                            if (m_pCache->must_refresh(m_key, this))
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
                                fetch_from_server = true;
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
                                fetch_from_server = false;
                            }
                        }
                        else
                        {
                            if (log_decisions())
                            {
                                MXS_NOTICE("Using fresh data from cache.");
                            }
                            fetch_from_server = false;
                        }
                    }
                    else
                    {
                        fetch_from_server = true;
                    }

                    if (fetch_from_server)
                    {
                        m_state = CACHE_EXPECTING_RESPONSE;
                    }
                    else
                    {
                        m_state = CACHE_EXPECTING_NOTHING;
                        gwbuf_free(pPacket);
                        DCB *dcb = m_pSession->client_dcb;

                        // TODO: This is not ok. Any filters before this filter, will not
                        // TODO: see this data.
                        rv = dcb->func.write(dcb, pResponse);
                    }
                }
            }
            else
            {
                m_state = CACHE_IGNORING_RESPONSE;
            }
        }
        break;

    default:
        break;
    }

    if (fetch_from_server)
    {
        rv = m_down.routeQuery(pPacket);
    }

    return rv;
}

int CacheFilterSession::clientReply(GWBUF* pData)
{
    int rv;

    if (m_res.pData)
    {
        gwbuf_append(m_res.pData, pData);
    }
    else
    {
        m_res.pData = pData;
    }

    if (m_state != CACHE_IGNORING_RESPONSE)
    {
        if (cache_max_resultset_size_exceeded(m_pCache->config(), gwbuf_length(m_res.pData)))
        {
            if (log_decisions())
            {
                MXS_NOTICE("Current size %uB of resultset, at least as much "
                           "as maximum allowed size %luKiB. Not caching.",
                           gwbuf_length(m_res.pData),
                           m_pCache->config().max_resultset_size / 1024);
            }

            m_state = CACHE_IGNORING_RESPONSE;
        }
    }

    switch (m_state)
    {
    case CACHE_EXPECTING_FIELDS:
        rv = handle_expecting_fields();
        break;

    case CACHE_EXPECTING_NOTHING:
        rv = handle_expecting_nothing();
        break;

    case CACHE_EXPECTING_RESPONSE:
        rv = handle_expecting_response();
        break;

    case CACHE_EXPECTING_ROWS:
        rv = handle_expecting_rows();
        break;

    case CACHE_EXPECTING_USE_RESPONSE:
        rv = handle_expecting_use_response();
        break;

    case CACHE_IGNORING_RESPONSE:
        rv = handle_ignoring_response();
        break;

    default:
        MXS_ERROR("Internal cache logic broken, unexpected state: %d", m_state);
        ss_dassert(!true);
        rv = send_upstream();
        reset_response_state();
        m_state = CACHE_IGNORING_RESPONSE;
    }

    return rv;
}

void CacheFilterSession::diagnostics(DCB* pDcb)
{
    // Not printing anything. Session of the same instance share the same cache, in
    // which case the same information would be printed once per session, or all
    // threads (but not sessions) share the same cache, in which case the output
    // would be nonsensical.
    dcb_printf(pDcb, "\n");
}

/**
 * Called when resultset field information is handled.
 */
int CacheFilterSession::handle_expecting_fields()
{
    ss_dassert(m_state == CACHE_EXPECTING_FIELDS);
    ss_dassert(m_res.pData);

    int rv = 1;

    bool insufficient = false;

    size_t buflen = gwbuf_length(m_res.pData);

    while (!insufficient && (buflen - m_res.offset >= MYSQL_HEADER_LEN))
    {
        uint8_t header[MYSQL_HEADER_LEN + 1];
        gwbuf_copy_data(m_res.pData, m_res.offset, MYSQL_HEADER_LEN + 1, header);

        size_t packetlen = MYSQL_HEADER_LEN + MYSQL_GET_PAYLOAD_LEN(header);

        if (m_res.offset + packetlen <= buflen)
        {
            // We have at least one complete packet.
            int command = (int)MYSQL_GET_COMMAND(header);

            switch (command)
            {
            case MYSQL_REPLY_EOF: // The EOF after the fields.
                m_res.offset += packetlen;
                m_state = CACHE_EXPECTING_ROWS;
                rv = handle_expecting_rows();
                break;

            default: // Field information.
                m_res.offset += packetlen;
                ++m_res.nFields;
                ss_dassert(m_res.nFields <= m_res.nTotalFields);
                break;
            }
        }
        else
        {
            // We need more data
            insufficient = true;
        }
    }

    return rv;
}

/**
 * Called when data is received (even if nothing is expected) from the server.
 */
int CacheFilterSession::handle_expecting_nothing()
{
    ss_dassert(m_state == CACHE_EXPECTING_NOTHING);
    ss_dassert(m_res.pData);
    MXS_ERROR("Received data from the backend althoug we were expecting nothing.");
    ss_dassert(!true);

    return send_upstream();
}

/**
 * Called when a response is received from the server.
 */
int CacheFilterSession::handle_expecting_response()
{
    ss_dassert(m_state == CACHE_EXPECTING_RESPONSE);
    ss_dassert(m_res.pData);

    int rv = 1;

    size_t buflen = gwbuf_length(m_res.pData);

    if (buflen >= MYSQL_HEADER_LEN + 1) // We need the command byte.
    {
        // Reserve enough space to accomodate for the largest length encoded integer,
        // which is type field + 8 bytes.
        uint8_t header[MYSQL_HEADER_LEN + 1 + 8];
        gwbuf_copy_data(m_res.pData, 0, MYSQL_HEADER_LEN + 1, header);

        switch ((int)MYSQL_GET_COMMAND(header))
        {
        case MYSQL_REPLY_OK:
            store_result();
        case MYSQL_REPLY_ERR:
            rv = send_upstream();
            m_state = CACHE_IGNORING_RESPONSE;
            break;

        case MYSQL_REPLY_LOCAL_INFILE: // GET_MORE_CLIENT_DATA/SEND_MORE_CLIENT_DATA
            rv = send_upstream();
            m_state = CACHE_IGNORING_RESPONSE;
            break;

        default:
            if (m_res.nTotalFields != 0)
            {
                // We've seen the header and have figured out how many fields there are.
                m_state = CACHE_EXPECTING_FIELDS;
                rv = handle_expecting_fields();
            }
            else
            {
                // mxs_leint_bytes() returns the length of the int type field + the size of the
                // integer.
                size_t n_bytes = mxs_leint_bytes(&header[4]);

                if (MYSQL_HEADER_LEN + n_bytes <= buflen)
                {
                    // Now we can figure out how many fields there are, but first we
                    // need to copy some more data.
                    gwbuf_copy_data(m_res.pData,
                                    MYSQL_HEADER_LEN + 1, n_bytes - 1, &header[MYSQL_HEADER_LEN + 1]);

                    m_res.nTotalFields = mxs_leint_value(&header[4]);
                    m_res.offset = MYSQL_HEADER_LEN + n_bytes;

                    m_state = CACHE_EXPECTING_FIELDS;
                    rv = handle_expecting_fields();
                }
                else
                {
                    // We need more data. We will be called again, when data is available.
                }
            }
            break;
        }
    }

    return rv;
}

/**
 * Called when resultset rows are handled.
 */
int CacheFilterSession::handle_expecting_rows()
{
    ss_dassert(m_state == CACHE_EXPECTING_ROWS);
    ss_dassert(m_res.pData);

    int rv = 1;

    bool insufficient = false;

    size_t buflen = gwbuf_length(m_res.pData);

    while (!insufficient && (buflen - m_res.offset >= MYSQL_HEADER_LEN))
    {
        uint8_t header[MYSQL_HEADER_LEN + 1];
        gwbuf_copy_data(m_res.pData, m_res.offset, MYSQL_HEADER_LEN + 1, header);

        size_t packetlen = MYSQL_HEADER_LEN + MYSQL_GET_PAYLOAD_LEN(header);

        if (m_res.offset + packetlen <= buflen)
        {
            if ((packetlen == MYSQL_EOF_PACKET_LEN) && (MYSQL_GET_COMMAND(header) == MYSQL_REPLY_EOF))
            {
                // The last EOF packet
                m_res.offset += packetlen;
                ss_dassert(m_res.offset == buflen);

                store_result();

                rv = send_upstream();
                m_state = CACHE_EXPECTING_NOTHING;
            }
            else
            {
                // Length encode strings, 0xfb denoting NULL.
                m_res.offset += packetlen;
                ++m_res.nRows;

                if (cache_max_resultset_rows_exceeded(m_pCache->config(), m_res.nRows))
                {
                    if (log_decisions())
                    {
                        MXS_NOTICE("Max rows %lu reached, not caching result.", m_res.nRows);
                    }
                    rv = send_upstream();
                    m_res.offset = buflen; // To abort the loop.
                    m_state = CACHE_IGNORING_RESPONSE;
                }
            }
        }
        else
        {
            // We need more data
            insufficient = true;
        }
    }

    return rv;
}

/**
 * Called when a response to a "USE db" is received from the server.
 */
int CacheFilterSession::handle_expecting_use_response()
{
    ss_dassert(m_state == CACHE_EXPECTING_USE_RESPONSE);
    ss_dassert(m_res.pData);

    int rv = 1;

    size_t buflen = gwbuf_length(m_res.pData);

    if (buflen >= MYSQL_HEADER_LEN + 1) // We need the command byte.
    {
        uint8_t command;

        gwbuf_copy_data(m_res.pData, MYSQL_HEADER_LEN, 1, &command);

        switch (command)
        {
        case MYSQL_REPLY_OK:
            // In case m_zUseDb could not be allocated in routeQuery(), we will
            // in fact reset the default db here. That's ok as it will prevent broken
            // entries in the cache.
            MXS_FREE(m_zDefaultDb);
            m_zDefaultDb = m_zUseDb;
            m_zUseDb = NULL;
            break;

        case MYSQL_REPLY_ERR:
            MXS_FREE(m_zUseDb);
            m_zUseDb = NULL;
            break;

        default:
            MXS_ERROR("\"USE %s\" received unexpected server response %d.",
                      m_zUseDb ? m_zUseDb : "<db>", command);
            MXS_FREE(m_zDefaultDb);
            MXS_FREE(m_zUseDb);
            m_zDefaultDb = NULL;
            m_zUseDb = NULL;
        }

        rv = send_upstream();
        m_state = CACHE_IGNORING_RESPONSE;
    }

    return rv;
}

/**
 * Called when all data from the server is ignored.
 */
int CacheFilterSession::handle_ignoring_response()
{
    ss_dassert(m_state == CACHE_IGNORING_RESPONSE);
    ss_dassert(m_res.pData);

    return send_upstream();
}

/**
 * Send data upstream.
 *
 * @return Whatever the upstream returns.
 */
int CacheFilterSession::send_upstream()
{
    ss_dassert(m_res.pData != NULL);

    int rv = m_up.clientReply(m_res.pData);
    m_res.pData = NULL;

    return rv;
}

/**
 * Reset cache response state
 */
void CacheFilterSession::reset_response_state()
{
    m_res.pData = NULL;
    m_res.nTotalFields = 0;
    m_res.nFields = 0;
    m_res.nRows = 0;
    m_res.offset = 0;
}

/**
 * Route a query via the cache.
 *
 * @param key A SELECT packet.
 * @param value The result.
 * @return True if the query was satisfied from the query.
 */
cache_result_t CacheFilterSession::get_cached_response(const GWBUF *pQuery, GWBUF **ppResponse)
{
    cache_result_t result = m_pCache->get_key(m_zDefaultDb, pQuery, &m_key);

    if (CACHE_RESULT_IS_OK(result))
    {
        uint32_t flags = CACHE_FLAGS_INCLUDE_STALE;

        result = m_pCache->get_value(m_key, flags, ppResponse);
    }
    else
    {
        MXS_ERROR("Could not create cache key.");
    }

    return result;
}

/**
 * Store the data.
 *
 * @param csdata Session data
 */
void CacheFilterSession::store_result()
{
    ss_dassert(m_res.pData);

    GWBUF *pData = gwbuf_make_contiguous(m_res.pData);

    if (pData)
    {
        m_res.pData = pData;

        cache_result_t result = m_pCache->put_value(m_key, m_res.pData);

        if (!CACHE_RESULT_IS_OK(result))
        {
            MXS_ERROR("Could not store cache item, deleting it.");

            result = m_pCache->del_value(m_key);

            if (!CACHE_RESULT_IS_OK(result) || !CACHE_RESULT_IS_NOT_FOUND(result))
            {
                MXS_ERROR("Could not delete cache item.");
            }
        }
    }

    if (m_refreshing)
    {
        m_pCache->refreshed(m_key, this);
        m_refreshing = false;
    }
}

/**
 * Whether the cache should be consulted.
 *
 * @param pParam The GWBUF being handled.
 *
 * @return True, if the cache should be consulted, false otherwise.
 */
bool CacheFilterSession::should_consult_cache(GWBUF* pPacket)
{
    bool consult_cache = false;

    uint32_t type_mask = qc_get_type_mask(pPacket);

    if (qc_query_is_type(type_mask, QUERY_TYPE_BEGIN_TRX))
    {
        // When a transaction is started, we initially assume it is read-only.
        m_is_read_only = true;
    }
    else if (!qc_query_is_type(type_mask, QUERY_TYPE_READ))
    {
        // Thereafter, if there's any non-read statement we mark it as non-readonly.
        // Note that the state of m_is_read_only is not consulted if there is no
        // on-going transaction of if there is an explicitly read-only transaction.
        m_is_read_only = false;
    }

    if (!session_trx_is_active(m_pSession))
    {
        if (log_decisions())
        {
            MXS_NOTICE("Cache can be used and stored to, since there is no transaction.");
        }
        consult_cache = true;
    }
    else if (session_trx_is_read_only(m_pSession))
    {
        if (log_decisions())
        {
            MXS_NOTICE("Cache can be used and stored to since there is an explicitly "
                       "read-only transaction.");
        }
        consult_cache = true;
    }
    else if (m_is_read_only)
    {
        if (log_decisions())
        {
            MXS_NOTICE("Cache can be used and stored to, since the current transaction "
                       "(not explicitly read-only) has so far been read-only.");
        }
        consult_cache = true;
    }
    else
    {
        if (log_decisions())
        {
            MXS_NOTICE("Cache can not be used, since a not explicitly read-only transaction "
                       "is active and the transaction has executed non-read statements.");
        }
    }

    if (consult_cache)
    {
        // We do not care whether the query was fully parsed or not.
        // If a query cannot be fully parsed, the worst thing that can
        // happen is that caching is not used, even though it would be
        // possible.
        if (qc_get_operation(pPacket) != QUERY_OP_SELECT)
        {
            consult_cache = false;
        }
    }

    return consult_cache;
}
