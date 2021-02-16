/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "schemarouter.hh"
#include "schemaroutersession.hh"
#include "schemarouterinstance.hh"

#include <inttypes.h>
#include <unordered_set>

#include <maxbase/atomic.hh>
#include <maxbase/alloc.h>
#include <maxscale/modutil.hh>
#include <maxscale/poll.hh>
#include <maxscale/protocol/mariadb/query_classifier.hh>
#include <maxscale/protocol/mariadb/resultset.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

#include <mysqld_error.h>

namespace schemarouter
{

bool connect_backend_servers(SRBackendList& backends, MXS_SESSION* session);

enum route_target get_shard_route_target(uint32_t qtype);
bool              change_current_db(std::string& dest, Shard& shard, GWBUF* buf);
bool              extract_database(GWBUF* buf, char* str);
bool              detect_show_shards(GWBUF* query);
void              write_error_to_client(MariaDBClientConnection* conn, int errnum,
                                        const char* mysqlstate, const char* errmsg);

SchemaRouterSession::SchemaRouterSession(MXS_SESSION* session,
                                         SchemaRouter* router,
                                         SRBackendList backends)
    : mxs::RouterSession(session)
    , m_closed(false)
    , m_client(static_cast<MariaDBClientConnection*>(session->client_connection()))
    , m_backends(std::move(backends))
    , m_config(*router->m_config.values())
    , m_router(router)
    , m_shard(m_router->m_shard_manager.get_shard(get_cache_key(), m_config.refresh_interval.count()))
    , m_state(0)
    , m_load_target(NULL)
{
    m_mysql_session = static_cast<MYSQL_session*>(session->protocol_data());
    auto current_db = m_mysql_session->db;

    // TODO: The following is not pretty and is bound to cause problems in the future.

    /* To enable connecting directly to a sharded database we first need
     * to disable it for the client DCB's protocol so that we can connect to them */
    if (m_mysql_session->client_capabilities() & GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB && !current_db.empty())
    {
        m_mysql_session->client_info.m_client_capabilities &= ~GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB;
        m_mysql_session->db.clear();

        /* Store the database the client is connecting to */
        m_connect_db = current_db;
        m_state |= INIT_USE_DB;

        MXS_INFO("Client logging in directly to a database '%s', "
                 "postponing until databases have been mapped.", current_db.c_str());
    }

    mxb::atomic::add(&m_router->m_stats.sessions, 1);
}

SchemaRouterSession::~SchemaRouterSession()
{
    mxb_assert(!m_closed);

    /**
     * Lock router client session for secure read and update.
     */
    if (!m_closed)
    {
        m_closed = true;

        for (const auto& a : m_backends)
        {
            if (a->in_use())
            {
                a->close();
            }
        }

        std::lock_guard<std::mutex> guard(m_router->m_lock);

        if (m_router->m_stats.longest_sescmd < m_stats.longest_sescmd)
        {
            m_router->m_stats.longest_sescmd = m_stats.longest_sescmd;
        }
        double ses_time = difftime(time(NULL), m_pSession->stats.connect);
        if (m_router->m_stats.ses_longest < ses_time)
        {
            m_router->m_stats.ses_longest = ses_time;
        }
        if (m_router->m_stats.ses_shortest > ses_time && m_router->m_stats.ses_shortest > 0)
        {
            m_router->m_stats.ses_shortest = ses_time;
        }

        m_router->m_stats.ses_average =
            (ses_time + ((m_router->m_stats.sessions - 1) * m_router->m_stats.ses_average))
            / (m_router->m_stats.sessions);
    }
}

static void inspect_query(GWBUF* pPacket, uint32_t* type, qc_query_op_t* op, uint8_t* command)
{
    uint8_t* data = GWBUF_DATA(pPacket);
    *command = data[4];

    switch (*command)
    {
    case MXS_COM_QUIT:          /*< 1 QUIT will close all sessions */
    case MXS_COM_INIT_DB:       /*< 2 DDL must go to the master */
    case MXS_COM_REFRESH:       /*< 7 - I guess this is session but not sure */
    case MXS_COM_DEBUG:         /*< 0d all servers dump debug info to stdout */
    case MXS_COM_PING:          /*< 0e all servers are pinged */
    case MXS_COM_CHANGE_USER:   /*< 11 all servers change it accordingly */
        // case MXS_COM_STMT_CLOSE: /*< free prepared statement */
        // case MXS_COM_STMT_SEND_LONG_DATA: /*< send data to column */
        // case MXS_COM_STMT_RESET: /*< resets the data of a prepared statement */
        *type = QUERY_TYPE_SESSION_WRITE;
        break;

    case MXS_COM_CREATE_DB: /**< 5 DDL must go to the master */
    case MXS_COM_DROP_DB:   /**< 6 DDL must go to the master */
        *type = QUERY_TYPE_WRITE;
        break;

    case MXS_COM_QUERY:
        *type = qc_get_type_mask(pPacket);
        *op = qc_get_operation(pPacket);
        break;

    case MXS_COM_STMT_PREPARE:
        *type = qc_get_type_mask(pPacket);
        *type |= QUERY_TYPE_PREPARE_STMT;
        break;

    case MXS_COM_STMT_EXECUTE:
        /** Parsing is not needed for this type of packet */
        *type = QUERY_TYPE_EXEC_STMT;
        break;

    case MXS_COM_SHUTDOWN:      /**< 8 where should shutdown be routed ? */
    case MXS_COM_STATISTICS:    /**< 9 ? */
    case MXS_COM_PROCESS_INFO:  /**< 0a ? */
    case MXS_COM_CONNECT:       /**< 0b ? */
    case MXS_COM_PROCESS_KILL:  /**< 0c ? */
    case MXS_COM_TIME:          /**< 0f should this be run in gateway ? */
    case MXS_COM_DELAYED_INSERT:/**< 10 ? */
    case MXS_COM_DAEMON:        /**< 1d ? */
    default:
        break;
    }

    if (mxs_log_is_priority_enabled(LOG_INFO))
    {
        char* sql;
        int sql_len;
        char* qtypestr = qc_typemask_to_string(*type);
        int rc = modutil_extract_SQL(pPacket, &sql, &sql_len);

        MXS_INFO("> Command: %s, stmt: %.*s %s%s",
                 STRPACKETTYPE(*command),
                 rc ? sql_len : 0,
                 rc ? sql : "",
                 (pPacket->hint == NULL ? "" : ", Hint:"),
                 (pPacket->hint == NULL ? "" : STRHINTTYPE(pPacket->hint->type)));

        MXS_FREE(qtypestr);
    }
}

mxs::Target* SchemaRouterSession::resolve_query_target(GWBUF* pPacket, uint32_t type,
                                                       uint8_t command, enum route_target& route_target)
{
    mxs::Target* target = NULL;

    if (route_target != TARGET_NAMED_SERVER)
    {
        /** We either don't know or don't care where this query should go */
        target = get_shard_target(pPacket, type);

        if (target && target->is_usable())
        {
            route_target = TARGET_NAMED_SERVER;
        }
    }

    if (TARGET_IS_UNDEFINED(route_target))
    {
        /** We don't know where to send this. Route it to either the server with
         * the current default database or to the first available server. */
        target = get_shard_target(pPacket, type);

        if ((!target && command != MXS_COM_INIT_DB && m_current_db.empty())
            || command == MXS_COM_FIELD_LIST
            || m_current_db.empty())
        {
            /** No current database and no databases in query or the database is
             * ignored, route to first available backend. */
            route_target = TARGET_ANY;
        }
    }

    if (TARGET_IS_ANY(route_target))
    {
        for (const auto& b : m_backends)
        {
            if (b->target()->is_usable())
            {
                route_target = TARGET_NAMED_SERVER;
                target = b->target();
                break;
            }
        }

        if (TARGET_IS_ANY(route_target))
        {
            /**No valid backends alive*/
            MXS_ERROR("Failed to route query, no backends are available.");
        }
    }

    return target;
}

static bool is_empty_packet(GWBUF* pPacket)
{
    bool rval = false;
    uint8_t len[3];

    if (gwbuf_length(pPacket) == 4
        && gwbuf_copy_data(pPacket, 0, 3, len) == 3
        && gw_mysql_get_byte3(len) == 0)
    {
        rval = true;
    }

    return rval;
}

int32_t SchemaRouterSession::routeQuery(GWBUF* pPacket)
{
    if (m_closed)
    {
        return 0;
    }

    if (m_shard.empty() && (m_state & INIT_MAPPING) == 0)
    {
        /* Generate database list */
        query_databases();
    }

    int ret = 0;

    /**
     * If the databases are still being mapped or if the client connected
     * with a default database but no database mapping was performed we need
     * to store the query. Once the databases have been mapped and/or the
     * default database is taken into use we can send the query forward.
     */
    if (m_state & (INIT_MAPPING | INIT_USE_DB))
    {
        m_queue.push_back(pPacket);
        ret = 1;

        if (m_state == (INIT_READY | INIT_USE_DB))
        {
            /**
             * This state is possible if a client connects with a default database
             * and the shard map was found from the router cache
             */
            if (!handle_default_db())
            {
                ret = 0;
            }
        }
        return ret;
    }

    uint8_t command = 0;
    mxs::Target* target = NULL;
    uint32_t type = QUERY_TYPE_UNKNOWN;
    qc_query_op_t op = QUERY_OP_UNDEFINED;
    enum route_target route_target = TARGET_UNDEFINED;

    if (m_load_target)
    {
        /** A load data local infile is active */
        target = m_load_target;
        route_target = TARGET_NAMED_SERVER;

        if (is_empty_packet(pPacket))
        {
            m_load_target = NULL;
        }
    }
    else
    {
        inspect_query(pPacket, &type, &op, &command);

        /** Create the response to the SHOW DATABASES from the mapped databases */
        if (qc_query_is_type(type, QUERY_TYPE_SHOW_DATABASES))
        {
            send_databases();
            gwbuf_free(pPacket);
            return 1;
        }
        else if (detect_show_shards(pPacket))
        {
            if (send_shards())
            {
                ret = 1;
            }
            gwbuf_free(pPacket);
            return ret;
        }

        /** The default database changes must be routed to a specific server */
        if (command == MXS_COM_INIT_DB || op == QUERY_OP_CHANGE_DB)
        {
            if (!change_current_db(m_current_db, m_shard, pPacket))
            {
                char db[MYSQL_DATABASE_MAXLEN + 1];
                extract_database(pPacket, db);
                gwbuf_free(pPacket);

                char errbuf[128 + MYSQL_DATABASE_MAXLEN];
                snprintf(errbuf, sizeof(errbuf), "Unknown database: %s", db);

                if (m_config.debug)
                {
                    sprintf(errbuf + strlen(errbuf), " ([%" PRIu64 "]: DB change failed)", m_pSession->id());
                }

                write_error_to_client(m_client, SCHEMA_ERR_DBNOTFOUND, SCHEMA_ERRSTR_DBNOTFOUND, errbuf);
                return 1;
            }

            route_target = TARGET_UNDEFINED;
            target = m_shard.get_location(m_current_db);

            if (target)
            {
                MXS_INFO("INIT_DB for database '%s' on server '%s'",
                         m_current_db.c_str(),
                         target->name());
                route_target = TARGET_NAMED_SERVER;
            }
            else
            {
                MXS_INFO("INIT_DB with unknown database");
            }
        }
        else
        {
            route_target = get_shard_route_target(type);
        }

        /**
         * Find a suitable server that matches the requirements of @c route_target
         */

        if (TARGET_IS_ALL(route_target))
        {
            /** Session commands, route to all servers */
            if (route_session_write(pPacket, command))
            {
                mxb::atomic::add(&m_router->m_stats.n_sescmd, 1, mxb::atomic::RELAXED);
                mxb::atomic::add(&m_router->m_stats.n_queries, 1, mxb::atomic::RELAXED);
                ret = 1;
            }
        }
        else if (target == NULL)
        {
            target = resolve_query_target(pPacket, type, command, route_target);
        }
    }

    if (TARGET_IS_NAMED_SERVER(route_target) && target)
    {
        if (SRBackend* bref = get_shard_backend(target->name()))
        {
            if (op == QUERY_OP_LOAD_LOCAL)
            {
                m_load_target = bref->target();
            }

            MXS_INFO("Route query to \t%s <", bref->name());

            uint8_t cmd = mxs_mysql_get_command(pPacket);

            auto responds = mxs_mysql_command_will_respond(cmd) ?
                mxs::Backend::EXPECT_RESPONSE :
                mxs::Backend::NO_RESPONSE;

            if (bref->write(pPacket, responds))
            {
                /** Add one query response waiter to backend reference */
                mxb::atomic::add(&m_router->m_stats.n_queries, 1, mxb::atomic::RELAXED);
                ret = 1;
            }
            else
            {
                gwbuf_free(pPacket);
            }
        }
    }

    return ret;
}
void SchemaRouterSession::handle_mapping_reply(SRBackend* bref, GWBUF** pPacket)
{
    int rc = inspect_mapping_states(bref, pPacket);

    if (rc == 1)
    {
        synchronize_shards();
        m_state &= ~INIT_MAPPING;

        /* Check if the session is reconnecting with a database name
         * that is not in the hashtable. If the database is not found
         * then close the session. */

        if (m_state & INIT_USE_DB)
        {
            if (!handle_default_db())
            {
                rc = -1;
            }
        }
        else if (m_queue.size() && rc != -1)
        {
            mxb_assert(m_state == INIT_READY || m_state == INIT_USE_DB);
            MXS_INFO("Routing stored query");
            route_queued_query();
        }
    }

    if (rc == -1)
    {
        m_pSession->kill();
    }
}

void SchemaRouterSession::handle_default_db_response()
{
    mxb_assert(m_num_init_db > 0);

    if (--m_num_init_db == 0)
    {
        m_state &= ~INIT_USE_DB;
        m_current_db = m_connect_db;
        mxb_assert(m_state == INIT_READY);

        if (m_queue.size())
        {
            route_queued_query();
        }
    }
}

namespace
{

mxs::Buffer::iterator skip_packet(mxs::Buffer::iterator it)
{
    uint32_t len = *it++;
    len |= (*it++) << 8;
    len |= (*it++) << 16;
    it.advance(len + 1);    // Payload length plus the fourth header byte (packet sequence)
    return it;
}

GWBUF* erase_last_packet(GWBUF* input)
{
    mxs::Buffer buf(input);
    auto it = buf.begin();
    auto end = it;

    while ((end = skip_packet(it)) != buf.end())
    {
        it = end;
    }

    buf.erase(it, end);
    return buf.release();
}
}

int32_t SchemaRouterSession::clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    SRBackend* bref = static_cast<SRBackend*>(down.back()->get_userdata());

    const auto& error = reply.error();

    if (error.is_unexpected_error())
    {
        // The connection was killed, we can safely ignore it. When the TCP connection is
        // closed, the router's error handling will sort it out.
        if (error.code() == ER_CONNECTION_KILLED)
        {
            bref->set_close_reason("Connection was killed");
        }
        else
        {
            mxb_assert(error.code() == ER_SERVER_SHUTDOWN
                       || error.code() == ER_NORMAL_SHUTDOWN
                       || error.code() == ER_SHUTDOWN_COMPLETE);
            bref->set_close_reason(std::string("Server '") + bref->name() + "' is shutting down");
        }

        // The server sent an error that we didn't expect: treat it as if the connection was closed. The
        // client shouldn't see this error as we can replace the closed connection.

        if (!(pPacket = erase_last_packet(pPacket)))
        {
            // Nothing to route to the client
            return 0;
        }
    }

    if (bref->should_ignore_response())
    {
        gwbuf_free(pPacket);
        pPacket = nullptr;
    }

    if (reply.is_complete())
    {
        MXS_INFO("Reply complete from '%s'", bref->name());
        bref->ack_write();
    }

    if (m_state & INIT_MAPPING)
    {
        handle_mapping_reply(bref, &pPacket);
    }
    else if (m_state & INIT_USE_DB)
    {
        MXS_INFO("Reply to USE '%s' received for session %p", m_connect_db.c_str(), m_pSession);
        gwbuf_free(pPacket);
        pPacket = NULL;
        handle_default_db_response();
    }
    else if (m_queue.size())
    {
        mxb_assert(m_state == INIT_READY);
        route_queued_query();
    }

    int32_t rc = 1;

    if (pPacket)
    {
        rc = RouterSession::clientReply(pPacket, down, reply);
    }

    return rc;
}

bool SchemaRouterSession::handleError(mxs::ErrorType type,
                                      GWBUF* pMessage,
                                      mxs::Endpoint* pProblem,
                                      const mxs::Reply& pReply)
{
    SRBackend* bref = static_cast<SRBackend*>(pProblem->get_userdata());
    mxb_assert(bref);

    if (bref->is_waiting_result())
    {
        if ((m_state & (INIT_USE_DB | INIT_MAPPING)) == INIT_USE_DB)
        {
            handle_default_db_response();
        }

        if ((m_state & INIT_MAPPING) == 0)
        {
            /** If the client is waiting for a reply, send an error. */
            mxs::ReplyRoute route;
            RouterSession::clientReply(gwbuf_clone(pMessage), route, mxs::Reply());
        }
    }

    bref->close(type == mxs::ErrorType::PERMANENT ? Backend::CLOSE_FATAL : Backend::CLOSE_NORMAL);

    return have_servers();
}

/**
 * Private functions
 */


/**
 * Synchronize the router client session shard map with the global shard map for
 * this user.
 *
 * If the router doesn't have a shard map for this user then the current shard map
 * of the client session is added to the m_router-> If the shard map in the router is
 * out of date, its contents are replaced with the contents of the current client
 * session. If the router has a usable shard map, the current shard map of the client
 * is discarded and the router's shard map is used.
 * @param client Router session
 */
void SchemaRouterSession::synchronize_shards()
{
    m_router->m_stats.shmap_cache_miss++;
    m_router->m_shard_manager.update_shard(m_shard, get_cache_key());
}

/**
 * Extract the database name from a COM_INIT_DB or literal USE ... query.
 * @param buf Buffer with the database change query
 * @param str Pointer where the database name is copied
 * @return True for success, false for failure
 */
bool extract_database(GWBUF* buf, char* str)
{
    uint8_t* packet;
    char* saved, * tok, * query = NULL;
    bool succp = true;
    unsigned int plen;

    packet = GWBUF_DATA(buf);
    plen = gw_mysql_get_byte3(packet) - 1;

    /** Copy database name from MySQL packet to session */
    if (mxs_mysql_get_command(buf) == MXS_COM_QUERY
        && qc_get_operation(buf) == QUERY_OP_CHANGE_DB)
    {
        const char* delim = "` \n\t;";

        query = modutil_get_SQL(buf);
        tok = strtok_r(query, delim, &saved);

        if (tok == NULL || strcasecmp(tok, "use") != 0)
        {
            MXS_ERROR("extract_database: Malformed chage database packet.");
            succp = false;
            goto retblock;
        }

        tok = strtok_r(NULL, delim, &saved);

        if (tok == NULL)
        {
            MXS_ERROR("extract_database: Malformed change database packet.");
            succp = false;
            goto retblock;
        }

        strncpy(str, tok, MYSQL_DATABASE_MAXLEN);
    }
    else
    {
        memcpy(str, packet + 5, plen);
        memset(str + plen, 0, 1);
    }
retblock:
    MXS_FREE(query);
    return succp;
}

bool SchemaRouterSession::write_session_command(SRBackend* backend, mxs::Buffer buffer, uint8_t cmd)
{
    bool ok = true;
    mxs::Backend::response_type type = mxs::Backend::NO_RESPONSE;

    if (mxs_mysql_command_will_respond(cmd))
    {
        type = backend == m_sescmd_replier ? mxs::Backend::EXPECT_RESPONSE : mxs::Backend::IGNORE_RESPONSE;
    }

    if (backend->write(buffer.release(), type))
    {
        MXS_INFO("Route query to %s: %s", backend->is_master() ? "master" : "slave", backend->name());
    }
    else
    {
        MXS_ERROR("Failed to execute session command in %s", backend->name());
        backend->close();
        ok = false;
    }

    return ok;
}

/**
 * Execute in backends used by current router session.
 * Save session variable commands to router session property
 * struct. Thus, they can be replayed in backends which are
 * started and joined later.
 *
 * Suppress redundant OK packets sent by backends.
 *
 * The first OK packet is replied to the client.
 * Return true if succeed, false is returned if router session was closed or
 * if execute_sescmd_in_backend failed.
 */
bool SchemaRouterSession::route_session_write(GWBUF* querybuf, uint8_t command)
{
    bool ok = false;
    mxs::Buffer buffer(querybuf);

    mxb::atomic::add(&m_stats.longest_sescmd, 1, mxb::atomic::RELAXED);

    for (const auto& b : m_backends)
    {
        if (b->in_use() && !m_sescmd_replier)
        {
            m_sescmd_replier = b.get();
        }
    }

    for (const auto& b : m_backends)
    {
        if (b->in_use() && write_session_command(b.get(), buffer, command))
        {
            if (b.get() == m_sescmd_replier)
            {
                ok = true;
            }
        }
    }

    return ok;
}

/**
 * Check if a router session has servers in use
 * @param rses Router client session
 * @return True if session has a single backend server in use that is running.
 * False if no backends are in use or running.
 */
bool SchemaRouterSession::have_servers()
{
    for (const auto& b : m_backends)
    {
        if (b->in_use() && !b->is_closed())
        {
            return true;
        }
    }

    return false;
}

/**
 * Detect if a query contains a SHOW SHARDS query.
 * @param query Query to inspect
 * @return true if the query is a SHOW SHARDS query otherwise false
 */
bool detect_show_shards(GWBUF* query)
{
    bool rval = false;
    char* querystr, * tok, * sptr;

    if (query == NULL)
    {
        MXS_ERROR("NULL value passed at %s:%d", __FILE__, __LINE__);
        return false;
    }

    if (!modutil_is_SQL(query) && !modutil_is_SQL_prepare(query))
    {
        return false;
    }

    if ((querystr = modutil_get_SQL(query)) == NULL)
    {
        MXS_ERROR("Failure to parse SQL at  %s:%d", __FILE__, __LINE__);
        return false;
    }

    tok = strtok_r(querystr, " ", &sptr);
    if (tok && strcasecmp(tok, "show") == 0)
    {
        tok = strtok_r(NULL, " ", &sptr);
        if (tok && strcasecmp(tok, "shards") == 0)
        {
            rval = true;
        }
    }

    MXS_FREE(querystr);
    return rval;
}

/**
 * Send a result set of all shards and their locations to the client.
 * @param rses Router client session
 * @return 0 on success, -1 on error
 */
bool SchemaRouterSession::send_shards()
{
    std::unique_ptr<ResultSet> set = ResultSet::create({"Database", "Server"});
    ServerMap pContent;
    m_shard.get_content(pContent);

    for (const auto& a : pContent)
    {
        set->add_row({a.first, a.second->name()});
    }

    const mxs::ReplyRoute down;
    const mxs::Reply reply;
    mxs::RouterSession::clientReply(set->as_buffer().release(), down, reply);

    return true;
}

void
write_error_to_client(MariaDBClientConnection* conn, int errnum, const char* mysqlstate, const char* errmsg)
{
    GWBUF* errbuff = modutil_create_mysql_err_msg(1, 0, errnum, mysqlstate, errmsg);
    if (errbuff)
    {
        if (conn->write(errbuff) != 1)
        {
            MXS_ERROR("Failed to write error packet to client.");
        }
    }
    else
    {
        MXS_ERROR("Memory allocation failed when creating error packet.");
    }
}

/**
 *
 * @param router_cli_ses
 * @return
 */
bool SchemaRouterSession::handle_default_db()
{
    bool rval = false;

    for (auto target : m_shard.get_all_locations(m_connect_db))
    {
        /* Send a COM_INIT_DB packet to the server with the right database
         * and set it as the client's active database */
        unsigned int qlen = m_connect_db.length();
        GWBUF* buffer = gwbuf_alloc(qlen + 5);
        uint8_t* data = GWBUF_DATA(buffer);

        gw_mysql_set_byte3(data, qlen + 1);
        data[3] = 0x0;
        data[4] = MXS_COM_INIT_DB;
        memcpy(data + 5, m_connect_db.c_str(), qlen);

        if (auto backend = get_shard_backend(target->name()))
        {
            backend->write(buffer);
            ++m_num_init_db;
            rval = true;
        }
    }

    if (!rval)
    {
        /** Unknown database, hang up on the client*/
        MXS_INFO("Connecting to a non-existent database '%s'", m_connect_db.c_str());
        char errmsg[128 + MYSQL_DATABASE_MAXLEN + 1];
        sprintf(errmsg, "Unknown database '%s'", m_connect_db.c_str());
        if (m_config.debug)
        {
            sprintf(errmsg + strlen(errmsg), " ([%" PRIu64 "]: DB not found on connect)", m_pSession->id());
        }
        write_error_to_client(m_client, SCHEMA_ERR_DBNOTFOUND, SCHEMA_ERRSTR_DBNOTFOUND, errmsg);
    }

    return rval;
}

void SchemaRouterSession::route_queued_query()
{
    GWBUF* tmp = m_queue.front().release();
    m_queue.pop_front();

#ifdef SS_DEBUG
    char* querystr = modutil_get_SQL(tmp);
    MXS_DEBUG("Sending queued buffer for session %p: %s", m_pSession, querystr);
    MXS_FREE(querystr);
#endif

    session_delay_routing(m_pSession, this, tmp, 0);
}

/**
 *
 * @param router_cli_ses Router client session
 * @return 1 if mapping is done, 0 if it is still ongoing and -1 on error
 */
int SchemaRouterSession::inspect_mapping_states(SRBackend* bref, GWBUF** wbuf)
{
    bool mapped = true;
    GWBUF* writebuf = *wbuf;

    for (const auto& b : m_backends)
    {
        if (b.get() == bref && !b->is_mapped())
        {
            enum showdb_response rc = parse_mapping_response(b.get(), &writebuf);

            if (rc == SHOWDB_FULL_RESPONSE)
            {
                b->set_mapped(true);
                MXS_DEBUG("Received SHOW DATABASES reply from '%s'", b->name());
            }
            else if (rc == SHOWDB_FATAL_ERROR)
            {
                *wbuf = writebuf;
                return -1;
            }
            else
            {
                mxb_assert(rc != SHOWDB_PARTIAL_RESPONSE);

                if ((m_state & INIT_FAILED) == 0)
                {
                    if (rc == SHOWDB_DUPLICATE_DATABASES)
                    {
                        MXS_ERROR("Duplicate tables found, closing session.");
                    }
                    else
                    {
                        MXS_ERROR("Fatal error when processing SHOW DATABASES response, closing session.");
                    }

                    /** This is the first response to the database mapping which
                     * has duplicate database conflict. Set the initialization bitmask
                     * to INIT_FAILED */
                    m_state |= INIT_FAILED;

                    /** Send the client an error about duplicate databases
                     * if there is a queued query from the client. */
                    if (m_queue.size())
                    {
                        auto err = modutil_create_mysql_err_msg(
                            1, 0, SCHEMA_ERR_DUPLICATEDB, SCHEMA_ERRSTR_DUPLICATEDB,
                            "Error: duplicate tables found on two different shards.");

                        mxs::ReplyRoute route;
                        RouterSession::clientReply(err, route, mxs::Reply());
                    }
                }

                *wbuf = writebuf;
                return -1;
            }
        }

        if (b->in_use() && !b->is_mapped())
        {
            mapped = false;
            MXS_DEBUG("Still waiting for reply to SHOW DATABASES from '%s'", b->name());
        }
    }

    *wbuf = writebuf;
    return mapped ? 1 : 0;
}

/**
 * Read new database name from COM_INIT_DB packet or a literal USE ... COM_QUERY
 * packet, check that it exists in the hashtable and copy its name to MYSQL_session.
 *
 * @param dest Destination where the database name will be written
 * @param dbhash Hashtable containing valid databases
 * @param buf   Buffer containing the database change query
 *
 * @return true if new database is set, false if non-existent database was tried
 * to be set
 */
bool change_current_db(std::string& dest, Shard& shard, GWBUF* buf)
{
    bool succp = false;
    char db[MYSQL_DATABASE_MAXLEN + 1];

    if (gwbuf_link_length(buf) <= MYSQL_DATABASE_MAXLEN - 5)
    {
        /** Copy database name from MySQL packet to session */
        if (extract_database(buf, db))
        {
            MXS_INFO("change_current_db: INIT_DB with database '%s'", db);
            /**
             * Update the session's active database only if it's in the hashtable.
             * If it isn't found, send a custom error packet to the client.
             */

            mxs::Target* target = shard.get_location(db);

            if (target)
            {
                dest = db;
                MXS_INFO("change_current_db: database is on server: '%s'.", target->name());
                succp = true;
            }
        }
    }
    else
    {
        MXS_ERROR("change_current_db: failed to change database: Query buffer too large");
    }

    return succp;
}

/**
 * Convert a length encoded string into a string.
 *
 * @param data Pointer to the first byte of the string
 *
 * @return String value
 */
std::string get_lenenc_str(uint8_t* ptr)
{
    if (*ptr < 251)
    {
        return std::string((char*)ptr + 1, *ptr);
    }
    else
    {
        switch (*(ptr))
        {
        case 0xfc:
            return std::string((char*)ptr + 2, mariadb::get_byte2(ptr));

        case 0xfd:
            return std::string((char*)ptr + 3, mariadb::get_byte3(ptr));

        case 0xfe:
            return std::string((char*)ptr + 8, mariadb::get_byte8(ptr));

        default:
            return "";
        }
    }
}

static const std::set<std::string> always_ignore = {"mysql", "information_schema", "performance_schema"};

bool SchemaRouterSession::ignore_duplicate_table(const std::string& data)
{
    bool rval = false;

    std::string db = data.substr(0, data.find("."));

    if (always_ignore.count(db))
    {
        rval = true;
    }

    if (!m_config.ignore_tables.empty())
    {
        auto it = std::find(m_config.ignore_tables.begin(), m_config.ignore_tables.end(), data);

        if (it != m_config.ignore_tables.end())
        {
            rval = true;
        }
    }

    if (!m_config.ignore_tables_regex.empty() && m_config.ignore_tables_regex.match(data))
    {
        rval = true;
    }

    return rval;
}

/**
 * Parses a response set to a SHOW DATABASES query and inserts them into the
 * router client session's database hashtable. The name of the database is used
 * as the key and the unique name of the server is the value. The function
 * currently supports only result sets that span a single SQL packet.
 * @param rses Router client session
 * @param target Target server where the database is
 * @param buf GWBUF containing the result set
 * @return 1 if a complete response was received, 0 if a partial response was received
 * and -1 if a database was found on more than one server.
 */
enum showdb_response SchemaRouterSession::parse_mapping_response(SRBackend* bref, GWBUF** buffer)
{
    bool duplicate_found = false;
    enum showdb_response rval = SHOWDB_PARTIAL_RESPONSE;

    if (buffer == NULL || *buffer == NULL)
    {
        return SHOWDB_FATAL_ERROR;
    }

    /** TODO: Don't make the buffer contiguous but process it as a buffer chain */
    *buffer = gwbuf_make_contiguous(*buffer);
    MXS_ABORT_IF_NULL(*buffer);
    GWBUF* buf = modutil_get_complete_packets(buffer);

    if (buf == NULL)
    {
        return SHOWDB_PARTIAL_RESPONSE;
    }
    int n_eof = 0;

    uint8_t* ptr = (uint8_t*) buf->start;

    if (PTR_IS_ERR(ptr))
    {
        MXS_ERROR("Mapping query returned an error; closing session.");
        gwbuf_free(buf);
        return SHOWDB_FATAL_ERROR;
    }

    if (n_eof == 0)
    {
        /** Skip column definitions */
        while (ptr < (uint8_t*) buf->end && !PTR_IS_EOF(ptr))
        {
            ptr += gw_mysql_get_byte3(ptr) + 4;
        }

        if (ptr >= (uint8_t*) buf->end)
        {
            MXS_INFO("Malformed packet for mapping query.");
            gwbuf_free(buf);
            return SHOWDB_FATAL_ERROR;
        }

        n_eof++;
        /** Skip first EOF packet */
        ptr += gw_mysql_get_byte3(ptr) + 4;
    }

    while (ptr < (uint8_t*) buf->end && !PTR_IS_EOF(ptr))
    {
        int payloadlen = gw_mysql_get_byte3(ptr);
        int packetlen = payloadlen + 4;
        auto data = get_lenenc_str(ptr + 4);
        mxs::Target* target = bref->target();

        if (!data.empty())
        {
            mxs::Target* duplicate = m_shard.get_location(data);

            if (duplicate && data.find('.') != std::string::npos && !ignore_duplicate_table(data))
            {
                duplicate_found = true;
                MXS_ERROR("'%s' found on servers '%s' and '%s' for user %s.",
                          data.c_str(), target->name(), duplicate->name(),
                          m_pSession->user_and_host().c_str());
            }
            else
            {
                m_shard.add_location(data, target);
            }

            MXS_INFO("<%s, %s>", target->name(), data.c_str());
        }

        ptr += packetlen;
    }

    if (ptr < (unsigned char*) buf->end && PTR_IS_EOF(ptr) && n_eof == 1)
    {
        n_eof++;
        MXS_INFO("SHOW DATABASES fully received from %s.", bref->name());
    }
    else
    {
        MXS_INFO("SHOW DATABASES partially received from %s.", bref->name());
    }

    gwbuf_free(buf);

    if (duplicate_found)
    {
        rval = SHOWDB_DUPLICATE_DATABASES;
    }
    else if (n_eof == 2)
    {
        rval = SHOWDB_FULL_RESPONSE;
    }

    return rval;
}

/**
 * Initiate the generation of the database hash table by sending a
 * SHOW DATABASES query to each valid backend server. This sets the session
 * into the mapping state where it queues further queries until all the database
 * servers have returned a result.
 * @param inst Router instance
 * @param session Router client session
 * @return 1 if all writes to backends were succesful and 0 if one or more errors occurred
 */
void SchemaRouterSession::query_databases()
{

    for (const auto& b : m_backends)
    {
        b->set_mapped(false);
    }

    mxb_assert((m_state & INIT_MAPPING) == 0);

    m_state |= INIT_MAPPING;
    m_state &= ~INIT_UNINT;

    GWBUF* buffer = modutil_create_query("SELECT CONCAT(schema_name, '.') FROM information_schema.schemata AS s "
                                         "LEFT JOIN information_schema.tables AS t ON s.schema_name = t.table_schema "
                                         "WHERE t.table_name IS NULL "
                                         "UNION "
                                         "SELECT CONCAT (table_schema, '.', table_name) FROM information_schema.tables");
    gwbuf_set_type(buffer, GWBUF_TYPE_COLLECT_RESULT);

    for (const auto& b : m_backends)
    {
        if (b->in_use() && !b->is_closed() && b->target()->is_usable())
        {
            GWBUF* clone = gwbuf_clone(buffer);
            MXS_ABORT_IF_NULL(clone);

            if (!b->write(clone))
            {
                MXS_ERROR("Failed to write mapping query to '%s'", b->name());
            }
        }
    }
    gwbuf_free(buffer);
}

/**
 * Check the hashtable for the right backend for this query.
 * @param router Router instance
 * @param client Client router session
 * @param buffer Query to inspect
 * @return Name of the backend or NULL if the query contains no known databases.
 */
mxs::Target* SchemaRouterSession::get_shard_target(GWBUF* buffer, uint32_t qtype)
{
    mxs::Target* rval = NULL;
    qc_query_op_t op = QUERY_OP_UNDEFINED;
    uint8_t command = mxs_mysql_get_command(buffer);

    if (command == MXS_COM_QUERY)
    {
        op = qc_get_operation(buffer);
        rval = get_query_target(buffer);
    }

    if (mxs_mysql_is_ps_command(command)
        || qc_query_is_type(qtype, QUERY_TYPE_PREPARE_NAMED_STMT)
        || qc_query_is_type(qtype, QUERY_TYPE_DEALLOC_PREPARE)
        || qc_query_is_type(qtype, QUERY_TYPE_PREPARE_STMT)
        || op == QUERY_OP_EXECUTE)
    {
        rval = get_ps_target(buffer, qtype, op);
    }

    if (buffer->hint && buffer->hint->type == HINT_ROUTE_TO_NAMED_SERVER)
    {
        for (const auto& b : m_backends)
        {
            if (strcasecmp(b->name(), (char*)buffer->hint->data) == 0)
            {
                rval = b->target();
                MXS_INFO("Routing hint found (%s)", rval->name());
            }
        }
    }

    if (rval == NULL && m_current_db.length())
    {
        /**
         * If the target name has not been found and the session has an
         * active database, set is as the target
         */
        rval = m_shard.get_location(m_current_db);

        if (rval)
        {
            MXS_INFO("Using active database '%s' on '%s'",
                     m_current_db.c_str(),
                     rval->name());
        }
    }
    return rval;
}

/**
 * Provide the router with a pointer to a suitable backend dcb.
 *
 * Detect failures in server statuses and reselect backends if necessary
 * If name is specified, server name becomes primary selection criteria.
 * Similarly, if max replication lag is specified, skip backends which lag too
 * much.
 *
 * @param p_dcb Address of the pointer to the resulting DCB
 * @param name  Name of the backend which is primarily searched. May be NULL.
 *
 * @return True if proper DCB was found, false otherwise.
 */
SRBackend* SchemaRouterSession::get_shard_backend(const char* name)
{
    SRBackend* rval = nullptr;

    for (const auto& b : m_backends)
    {
        if (b->in_use() && (strcasecmp(name, b->target()->name()) == 0)
            && b->target()->is_usable())
        {
            rval = b.get();
            break;
        }
    }

    return rval;
}


/**
 * Examine the query type, transaction state and routing hints. Find out the
 * target for query routing.
 *
 *  @param qtype      Type of query
 *  @param trx_active Is transacation active or not
 *  @param hint       Pointer to list of hints attached to the query buffer
 *
 *  @return bitfield including the routing target, or the target server name
 *          if the query would otherwise be routed to slave.
 */
enum route_target get_shard_route_target(uint32_t qtype)
{
    enum route_target target = TARGET_UNDEFINED;

    /**
     * These queries are not affected by hints
     */
    if (qc_query_is_type(qtype, QUERY_TYPE_SESSION_WRITE)
        || qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_WRITE)
        || qc_query_is_type(qtype, QUERY_TYPE_USERVAR_WRITE)
        || qc_query_is_type(qtype, QUERY_TYPE_ENABLE_AUTOCOMMIT)
        || qc_query_is_type(qtype, QUERY_TYPE_DISABLE_AUTOCOMMIT))
    {
        /** hints don't affect on routing */
        target = TARGET_ALL;
    }
    else if (qc_query_is_type(qtype, QUERY_TYPE_SYSVAR_READ)
             || qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_READ))
    {
        target = TARGET_ANY;
    }

    return target;
}

/**
 * Generates a custom SHOW DATABASES result set from all the databases in the
 * hashtable. Only backend servers that are up and in a proper state are listed
 * in it.
 * @param router Router instance
 * @param client Router client session
 * @return True if the sending of the database list was successful, otherwise false
 */
void SchemaRouterSession::send_databases()
{
    ServerMap dblist;
    std::set<std::string> db_names;
    m_shard.get_content(dblist);

    for (auto a : dblist)
    {
        std::string db = a.first.substr(0, a.first.find("."));
        db_names.insert(db);
    }

    std::unique_ptr<ResultSet> set = ResultSet::create({"Database"});

    for (const auto& name : db_names)
    {
        set->add_row({name});
    }

    const mxs::ReplyRoute down;
    const mxs::Reply reply;
    mxs::RouterSession::clientReply(set->as_buffer().release(), down, reply);
}

mxs::Target* SchemaRouterSession::get_query_target(GWBUF* buffer)
{
    auto tables = qc_get_table_names(buffer, true);
    mxs::Target* rval = NULL;

    for (auto& t : tables)
    {
        if (t.find('.') == std::string::npos)
        {
            t = m_current_db + '.' + t;
        }
    }

    if ((rval = m_shard.get_location(tables)))
    {
        MXS_INFO("Query targets table on server '%s'", rval->name());
    }
    else if ((rval = m_shard.get_location(qc_get_database_names(buffer))))
    {
        MXS_INFO("Query targets database on server '%s'", rval->name());
    }

    return rval;
}

mxs::Target* SchemaRouterSession::get_ps_target(GWBUF* buffer, uint32_t qtype, qc_query_op_t op)
{
    mxs::Target* rval = NULL;
    uint8_t command = mxs_mysql_get_command(buffer);

    if (qc_query_is_type(qtype, QUERY_TYPE_PREPARE_NAMED_STMT))
    {
        // If pStmt is null, the PREPARE was malformed. In that case it can be routed to any backend to get
        // a proper error response. Also returns null if preparing from a variable. This is a limitation.
        GWBUF* pStmt = qc_get_preparable_stmt(buffer);
        if (pStmt)
        {
            char* stmt = qc_get_prepare_name(buffer);

            if ((rval = m_shard.get_location(qc_get_table_names(pStmt, true))))
            {
                MXS_INFO("PREPARING NAMED %s ON SERVER %s", stmt, rval->name());
                m_shard.add_statement(stmt, rval);
            }
            MXS_FREE(stmt);
        }
    }
    else if (op == QUERY_OP_EXECUTE)
    {
        char* stmt = qc_get_prepare_name(buffer);
        mxs::Target* ps_target = m_shard.get_statement(stmt);
        if (ps_target)
        {
            rval = ps_target;
            MXS_INFO("Executing named statement %s on server %s", stmt, rval->name());
        }
        MXS_FREE(stmt);
    }
    else if (qc_query_is_type(qtype, QUERY_TYPE_DEALLOC_PREPARE))
    {
        char* stmt = qc_get_prepare_name(buffer);
        if ((rval = m_shard.get_statement(stmt)))
        {
            MXS_INFO("Closing named statement %s on server %s", stmt, rval->name());
            m_shard.remove_statement(stmt);
        }
        MXS_FREE(stmt);
    }
    else if (qc_query_is_type(qtype, QUERY_TYPE_PREPARE_STMT))
    {
        rval = m_shard.get_location(qc_get_table_names(buffer, true));

        if (rval)
        {
            mxb_assert(gwbuf_get_id(buffer) != 0);
            m_shard.add_statement(gwbuf_get_id(buffer), rval);
        }

        MXS_INFO("Prepare statement on server %s", rval ? rval->name() : "<no target found>");
    }
    else if (mxs_mysql_is_ps_command(command))
    {
        uint32_t id = mxs_mysql_extract_ps_id(buffer);
        rval = m_shard.get_statement(id);

        if (command == MXS_COM_STMT_CLOSE)
        {
            MXS_INFO("Closing prepared statement %d ", id);
            m_shard.remove_statement(id);
        }
    }
    return rval;
}

std::string SchemaRouterSession::get_cache_key() const
{
    std::string key = m_pSession->user();

    for (const auto& b : m_backends)
    {
        if (b->in_use())
        {
            key += b->name();
        }
    }

    return key;
}
}
