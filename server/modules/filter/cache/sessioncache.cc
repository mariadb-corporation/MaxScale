/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "sessioncache.h"
#include <new>
#include <maxscale/alloc.h>
#include <maxscale/query_classifier.h>
#include <maxscale/mysql_utils.h>
#include "storage.h"

SessionCache::SessionCache(Cache* pCache, SESSION* pSession, char* zDefaultDb)
    : m_state(CACHE_EXPECTING_NOTHING)
    , m_pCache(pCache)
    , m_pSession(pSession)
    , m_zDefaultDb(zDefaultDb)
    , m_zUseDb(NULL)
    , m_refreshing(false)
{
    memset(&m_down, 0, sizeof(m_down));
    memset(&m_up, 0, sizeof(m_up));
    memset(m_key.data, 0, CACHE_KEY_MAXLEN);

    reset_response_state();
}

SessionCache::~SessionCache()
{
    MXS_FREE(m_zUseDb);
    MXS_FREE(m_zDefaultDb);
}

//static
SessionCache* SessionCache::Create(Cache* pCache, SESSION* pSession)
{
    SessionCache* pSessionCache = NULL;

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
        pSessionCache = new (std::nothrow) SessionCache(pCache, pSession, zDefaultDb);

        if (!pSessionCache)
        {
            MXS_FREE(zDefaultDb);
        }
    }

    return pSessionCache;
}

void SessionCache::close()
{
}

void SessionCache::setDownstream(DOWNSTREAM* pDown)
{
    m_down = *pDown;
}

void SessionCache::setUpstream(UPSTREAM* pUp)
{
    m_up = *pUp;
}

int SessionCache::routeQuery(GWBUF* pPacket)
{
    uint8_t* pData = static_cast<uint8_t*>(GWBUF_DATA(pPacket));

    // All of these should be guaranteed by RCAP_TYPE_TRANSACTION_TRACKING
    ss_dassert(GWBUF_IS_CONTIGUOUS(pPacket));
    ss_dassert(GWBUF_LENGTH(pPacket) >= MYSQL_HEADER_LEN + 1);
    ss_dassert(MYSQL_GET_PACKET_LEN(pData) + MYSQL_HEADER_LEN == GWBUF_LENGTH(pPacket));

    bool fetch_from_server = true;

    reset_response_state();
    m_state = CACHE_IGNORING_RESPONSE;

    int rv;

    switch ((int)MYSQL_GET_COMMAND(pData))
    {
    case MYSQL_COM_INIT_DB:
        {
            ss_dassert(!m_zUseDb);
            size_t len = MYSQL_GET_PACKET_LEN(pData) - 1; // Remove the command byte.
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
        {
            // We do not care whether the query was fully parsed or not.
            // If a query cannot be fully parsed, the worst thing that can
            // happen is that caching is not used, even though it would be
            // possible.
            if (qc_get_operation(pPacket) == QUERY_OP_SELECT)
            {
                SESSION *session = m_pSession;

                if ((session_is_autocommit(session) && !session_trx_is_active(session)) ||
                    session_trx_is_read_only(session))
                {
                    if (m_pCache->should_store(m_zDefaultDb, pPacket))
                    {
                        if (m_pCache->should_use(m_pSession))
                        {
                            GWBUF* pResponse;
                            cache_result_t result = get_cached_response(pPacket, &pResponse);

                            switch (result)
                            {
                            case CACHE_RESULT_STALE:
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
                                        m_refreshing = true;
                                        fetch_from_server = true;
                                        break;
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
                                break;

                            case CACHE_RESULT_OK:
                                if (log_decisions())
                                {
                                    MXS_NOTICE("Using fresh data from cache.");
                                }
                                fetch_from_server = false;
                                break;

                            default:
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
                else
                {
                    if (log_decisions())
                    {
                        MXS_NOTICE("autocommit = %s and transaction state %s => Not using or "
                                   "storing to cache.",
                                   session_is_autocommit(m_pSession) ? "ON" : "OFF",
                                   session_trx_state_to_string(session_get_trx_state(m_pSession)));
                    }
                }
            }
            break;

        default:
            break;
        }
    }

    if (fetch_from_server)
    {
        rv = m_down.routeQuery(m_down.instance, m_down.session, pPacket);
    }

    return rv;
}

int SessionCache::clientReply(GWBUF* pData)
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
        if (gwbuf_length(m_res.pData) > m_pCache->config().max_resultset_size)
        {
            if (log_decisions())
            {
                MXS_NOTICE("Current size %uB of resultset, at least as much "
                           "as maximum allowed size %uKiB. Not caching.",
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

void SessionCache::diagnostics(DCB* pDcb)
{
    dcb_printf(pDcb, "Hello World from Cache!\n");
}

/**
 * Called when resultset field information is handled.
 */
int SessionCache::handle_expecting_fields()
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

        size_t packetlen = MYSQL_HEADER_LEN + MYSQL_GET_PACKET_LEN(header);

        if (m_res.offset + packetlen <= buflen)
        {
            // We have at least one complete packet.
            int command = (int)MYSQL_GET_COMMAND(header);

            switch (command)
            {
            case 0xfe: // EOF, the one after the fields.
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
int SessionCache::handle_expecting_nothing()
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
int SessionCache::handle_expecting_response()
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
        case 0x00: // OK
        case 0xff: // ERR
            store_result();

            rv = send_upstream();
            m_state = CACHE_IGNORING_RESPONSE;
            break;

        case 0xfb: // GET_MORE_CLIENT_DATA/SEND_MORE_CLIENT_DATA
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
                // leint_bytes() returns the length of the int type field + the size of the
                // integer.
                size_t n_bytes = leint_bytes(&header[4]);

                if (MYSQL_HEADER_LEN + n_bytes <= buflen)
                {
                    // Now we can figure out how many fields there are, but first we
                    // need to copy some more data.
                    gwbuf_copy_data(m_res.pData,
                                    MYSQL_HEADER_LEN + 1, n_bytes - 1, &header[MYSQL_HEADER_LEN + 1]);

                    m_res.nTotalFields = leint_value(&header[4]);
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
int SessionCache::handle_expecting_rows()
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

        size_t packetlen = MYSQL_HEADER_LEN + MYSQL_GET_PACKET_LEN(header);

        if (m_res.offset + packetlen <= buflen)
        {
            // We have at least one complete packet.
            int command = (int)MYSQL_GET_COMMAND(header);

            switch (command)
            {
            case 0xfe: // EOF, the one after the rows.
                m_res.offset += packetlen;
                ss_dassert(m_res.offset == buflen);

                store_result();

                rv = send_upstream();
                m_state = CACHE_EXPECTING_NOTHING;
                break;

            case 0xfb: // NULL
            default: // length-encoded-string
                m_res.offset += packetlen;
                ++m_res.nRows;

                if (m_res.nRows > m_pCache->config().max_resultset_rows)
                {
                    if (log_decisions())
                    {
                        MXS_NOTICE("Max rows %lu reached, not caching result.", m_res.nRows);
                    }
                    rv = send_upstream();
                    m_res.offset = buflen; // To abort the loop.
                    m_state = CACHE_IGNORING_RESPONSE;
                }
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
 * Called when a response to a "USE db" is received from the server.
 */
int SessionCache::handle_expecting_use_response()
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
        case 0x00: // OK
            // In case m_zUseDb could not be allocated in routeQuery(), we will
            // in fact reset the default db here. That's ok as it will prevent broken
            // entries in the cache.
            MXS_FREE(m_zDefaultDb);
            m_zDefaultDb = m_zUseDb;
            m_zUseDb = NULL;
            break;

        case 0xff: // ERR
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
int SessionCache::handle_ignoring_response()
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
int SessionCache::send_upstream()
{
    ss_dassert(m_res.pData != NULL);

    int rv = m_up.clientReply(m_up.instance, m_up.session, m_res.pData);
    m_res.pData = NULL;

    return rv;
}

/**
 * Reset cache response state
 */
void SessionCache::reset_response_state()
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
cache_result_t SessionCache::get_cached_response(const GWBUF *pQuery, GWBUF **ppResponse)
{
    cache_result_t result = m_pCache->get_key(m_zDefaultDb, pQuery, &m_key);

    if (result == CACHE_RESULT_OK)
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
void SessionCache::store_result()
{
    ss_dassert(m_res.pData);

    GWBUF *pData = gwbuf_make_contiguous(m_res.pData);

    if (pData)
    {
        m_res.pData = pData;

        cache_result_t result = m_pCache->put_value(m_key, m_res.pData);

        if (result != CACHE_RESULT_OK)
        {
            MXS_ERROR("Could not store cache item, deleting it.");

            result = m_pCache->del_value(m_key);

            if ((result != CACHE_RESULT_OK) || (result != CACHE_RESULT_NOT_FOUND))
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
