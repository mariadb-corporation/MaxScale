/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include "schemarouter.hh"
#include "schemaroutersession.hh"
#include "schemarouterinstance.hh"

#include <inttypes.h>

#include <maxbase/atomic.hh>
#include <maxscale/alloc.h>
#include <maxscale/modutil.h>
#include <maxscale/poll.h>
#include <maxscale/query_classifier.h>
#include <maxscale/resultset.hh>

namespace schemarouter
{

bool connect_backend_servers(SSRBackendList& backends, MXS_SESSION* session);

enum route_target get_shard_route_target(uint32_t qtype);
bool              change_current_db(std::string& dest, Shard& shard, GWBUF* buf);
bool              extract_database(GWBUF* buf, char* str);
bool              detect_show_shards(GWBUF* query);
void              write_error_to_client(DCB* dcb, int errnum, const char* mysqlstate, const char* errmsg);

SchemaRouterSession::SchemaRouterSession(MXS_SESSION* session,
                                         SchemaRouter* router,
                                         SSRBackendList& backends)
    : mxs::RouterSession(session)
    , m_closed(false)
    , m_client(session->client_dcb)
    , m_mysql_session((MYSQL_session*)session->client_dcb->data)
    , m_backends(backends)
    , m_config(router->m_config)
    , m_router(router)
    , m_shard(m_router->m_shard_manager.get_shard(m_client->user, m_config->refresh_min_interval))
    , m_state(0)
    , m_sent_sescmd(0)
    , m_replied_sescmd(0)
    , m_load_target(NULL)
{
    char db[MYSQL_DATABASE_MAXLEN + 1] = "";
    MySQLProtocol* protocol = (MySQLProtocol*)session->client_dcb->protocol;
    bool using_db = false;
    bool have_db = false;
    const char* current_db = mxs_mysql_get_current_db(session);

    /* To enable connecting directly to a sharded database we first need
     * to disable it for the client DCB's protocol so that we can connect to them*/
    if (protocol->client_capabilities & GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB
        && (have_db = *current_db))
    {
        protocol->client_capabilities &= ~GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB;
        strcpy(db, current_db);
        mxs_mysql_set_current_db(session, "");
        using_db = true;
        MXS_INFO("Client logging in directly to a database '%s', "
                 "postponing until databases have been mapped.",
                 db);
    }

    if (using_db)
    {
        m_state |= INIT_USE_DB;
    }

    if (db[0])
    {
        /* Store the database the client is connecting to */
        m_connect_db = db;
    }

    mxb::atomic::add(&m_router->m_stats.sessions, 1);
}

SchemaRouterSession::~SchemaRouterSession()
{
}

void SchemaRouterSession::close()
{
    mxb_assert(!m_closed);

    /**
     * Lock router client session for secure read and update.
     */
    if (!m_closed)
    {
        m_closed = true;

        for (SSRBackendList::iterator it = m_backends.begin(); it != m_backends.end(); it++)
        {
            SSRBackend& bref = *it;
            /** The backends are closed here to trigger the shutdown of
             * the connected DCBs */
            if (bref->in_use())
            {
                bref->close();
            }
        }

        std::lock_guard<std::mutex> guard(m_router->m_lock);

        if (m_router->m_stats.longest_sescmd < m_stats.longest_sescmd)
        {
            m_router->m_stats.longest_sescmd = m_stats.longest_sescmd;
        }
        double ses_time = difftime(time(NULL), m_client->session->stats.connect);
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

SERVER* SchemaRouterSession::resolve_query_target(GWBUF* pPacket,
                                                  uint32_t type,
                                                  uint8_t  command,
                                                  enum route_target& route_target)
{
    SERVER* target = NULL;

    if (route_target != TARGET_NAMED_SERVER)
    {
        /** We either don't know or don't care where this query should go */
        target = get_shard_target(pPacket, type);

        if (target && server_is_usable(target))
        {
            route_target = TARGET_NAMED_SERVER;
        }
    }

    if (TARGET_IS_UNDEFINED(route_target))
    {
        /** We don't know where to send this. Route it to either the server with
         * the current default database or to the first available server. */
        target = get_shard_target(pPacket, type);

        if ((target == NULL && command != MXS_COM_INIT_DB && m_current_db.length() == 0)
            || command == MXS_COM_FIELD_LIST
            || m_current_db.length() == 0)
        {
            /** No current database and no databases in query or the database is
             * ignored, route to first available backend. */
            route_target = TARGET_ANY;
        }
    }

    if (TARGET_IS_ANY(route_target))
    {
        for (SSRBackendList::iterator it = m_backends.begin(); it != m_backends.end(); it++)
        {
            SERVER* server = (*it)->backend()->server;
            if (server_is_usable(server))
            {
                route_target = TARGET_NAMED_SERVER;
                target = server;
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

    if (m_shard.empty())
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
    SERVER* target = NULL;
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
        else if (qc_query_is_type(type, QUERY_TYPE_SHOW_TABLES))
        {
            if (send_tables(pPacket))
            {
                gwbuf_free(pPacket);
                return 1;
            }
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

                if (m_config->debug)
                {
                    sprintf(errbuf + strlen(errbuf),
                            " ([%" PRIu64 "]: DB change failed)",
                            m_client->session->ses_id);
                }

                write_error_to_client(m_client,
                                      SCHEMA_ERR_DBNOTFOUND,
                                      SCHEMA_ERRSTR_DBNOTFOUND,
                                      errbuf);
                return 1;
            }

            route_target = TARGET_UNDEFINED;
            target = m_shard.get_location(m_current_db);

            if (target)
            {
                MXS_INFO("INIT_DB for database '%s' on server '%s'",
                         m_current_db.c_str(),
                         target->name);
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

    DCB* target_dcb = NULL;

    if (TARGET_IS_NAMED_SERVER(route_target) && target
        && get_shard_dcb(&target_dcb, target->name))
    {
        /** We know where to route this query */
        SSRBackend bref = get_bref_from_dcb(target_dcb);

        if (op == QUERY_OP_LOAD_LOCAL)
        {
            m_load_target = bref->backend()->server;
        }

        MXS_INFO("Route query to \t%s %s <", bref->name(), bref->uri());

        if (bref->has_session_commands())
        {
            /** Store current statement if execution of the previous
             * session command hasn't been completed. */
            bref->store_command(pPacket);
            pPacket = NULL;
            ret = 1;
        }
        else if (qc_query_is_type(type, QUERY_TYPE_PREPARE_STMT))
        {
            if (handle_statement(pPacket, bref, command, type))
            {
                mxb::atomic::add(&m_router->m_stats.n_sescmd, 1, mxb::atomic::RELAXED);
                mxb::atomic::add(&m_router->m_stats.n_queries, 1, mxb::atomic::RELAXED);
                ret = 1;
            }
        }
        else
        {
            uint8_t cmd = mxs_mysql_get_command(pPacket);

            auto responds = mxs_mysql_command_will_respond(cmd) ?
                mxs::Backend::EXPECT_RESPONSE :
                mxs::Backend::NO_RESPONSE;

            if (bref->write(pPacket, responds))
            {
                /** Add one query response waiter to backend reference */
                mxb::atomic::add(&m_router->m_stats.n_queries, 1, mxb::atomic::RELAXED);
                mxb::atomic::add(&bref->server()->stats.packets, 1, mxb::atomic::RELAXED);
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
void SchemaRouterSession::handle_mapping_reply(SSRBackend& bref, GWBUF** pPacket)
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
        poll_fake_hangup_event(m_client);
    }
}

void SchemaRouterSession::process_sescmd_response(SSRBackend& bref, GWBUF** ppPacket)
{
    mxb_assert(GWBUF_IS_COLLECTED_RESULT(*ppPacket));
    uint8_t command = bref->next_session_command()->get_command();
    uint64_t id = bref->complete_session_command();
    MXS_PS_RESPONSE resp = {};

    if (m_replied_sescmd < m_sent_sescmd && id == m_replied_sescmd + 1)
    {
        if (command == MXS_COM_STMT_PREPARE)
        {
            mxs_mysql_extract_ps_response(*ppPacket, &resp);
            MXS_INFO("ID: %lu HANDLE: %lu", (unsigned long)id, (unsigned long)resp.id);
            m_shard.add_ps_handle(id, resp.id);
            MXS_INFO("STMT SERVER: %s", bref->backend()->server->name);
            m_shard.add_statement(id, bref->backend()->server);
            uint8_t* ptr = GWBUF_DATA(*ppPacket) + MYSQL_PS_ID_OFFSET;
            gw_mysql_set_byte4(ptr, id);
        }
        /** First reply to this session command, route it to the client */
        ++m_replied_sescmd;
    }
    else
    {
        /** The reply to this session command has already been sent to
         * the client, discard it */
        gwbuf_free(*ppPacket);
        *ppPacket = NULL;
    }
}

void SchemaRouterSession::clientReply(GWBUF* pPacket, DCB* pDcb)
{
    SSRBackend bref = get_bref_from_dcb(pDcb);

    if (m_closed || bref.get() == NULL)     // The bref should always be valid
    {
        gwbuf_free(pPacket);
        return;
    }

    bref->process_reply(pPacket);

    if (m_state & INIT_MAPPING)
    {
        handle_mapping_reply(bref, &pPacket);
    }
    else if (m_state & INIT_USE_DB)
    {
        MXS_DEBUG("Reply to USE '%s' received for session %p",
                  m_connect_db.c_str(),
                  m_client->session);
        m_state &= ~INIT_USE_DB;
        m_current_db = m_connect_db;
        mxb_assert(m_state == INIT_READY);

        gwbuf_free(pPacket);
        pPacket = NULL;

        if (m_queue.size())
        {
            route_queued_query();
        }
    }

    else if (m_queue.size())
    {
        mxb_assert(m_state == INIT_READY);
        route_queued_query();
    }
    else if (bref->reply_is_complete())
    {
        if (bref->has_session_commands())
        {
            process_sescmd_response(bref, &pPacket);
        }

        if (bref->has_session_commands() && bref->execute_session_command())
        {
            MXS_INFO("Backend %s:%d processed reply and starts to execute active cursor.",
                     bref->backend()->server->address,
                     bref->backend()->server->port);
        }
        else if (bref->write_stored_command())
        {
            mxb::atomic::add(&m_router->m_stats.n_queries, 1, mxb::atomic::RELAXED);
        }
    }

    if (pPacket)
    {
        MXS_SESSION_ROUTE_REPLY(pDcb->session, pPacket);
    }
}

void SchemaRouterSession::handleError(GWBUF* pMessage,
                                      DCB*   pProblem,
                                      mxs_error_action_t action,
                                      bool* pSuccess)
{
    mxb_assert(pProblem->dcb_role == DCB_ROLE_BACKEND_HANDLER);
    SSRBackend bref = get_bref_from_dcb(pProblem);

    if (bref.get() == NULL)     // Should never happen
    {
        return;
    }

    switch (action)
    {
    case ERRACT_NEW_CONNECTION:
        if (bref->is_waiting_result())
        {
            /** If the client is waiting for a reply, send an error. */
            m_client->func.write(m_client, gwbuf_clone(pMessage));
        }

        *pSuccess = have_servers();
        break;

    case ERRACT_REPLY_CLIENT:
        // The session pointer can be NULL if the creation fails when filters are being set up
        if (m_client->session && m_client->session->state == SESSION_STATE_ROUTER_READY)
        {
            m_client->func.write(m_client, gwbuf_clone(pMessage));
        }

        *pSuccess = false;      /*< no new backend servers were made available */
        break;

    default:
        *pSuccess = false;
        break;
    }

    bref->close();
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
    m_router->m_shard_manager.update_shard(m_shard, m_client->user);
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
    bool succp = false;

    MXS_INFO("Session write, routing to all servers.");
    mxb::atomic::add(&m_stats.longest_sescmd, 1, mxb::atomic::RELAXED);

    /** Increment the session command count */
    ++m_sent_sescmd;

    for (SSRBackendList::iterator it = m_backends.begin(); it != m_backends.end(); it++)
    {
        if ((*it)->in_use())
        {
            GWBUF* buffer = gwbuf_clone(querybuf);

            (*it)->append_session_command(buffer, m_sent_sescmd);

            if (mxs_log_is_priority_enabled(LOG_INFO))
            {
                MXS_INFO("Route query to %s\t%s:%d",
                         server_is_master((*it)->backend()->server) ? "master" : "slave",
                         (*it)->backend()->server->address,
                         (*it)->backend()->server->port);
            }

            if ((*it)->session_command_count() == 1)
            {
                if ((*it)->execute_session_command())
                {
                    succp = true;
                    mxb::atomic::add(&(*it)->server()->stats.packets, 1, mxb::atomic::RELAXED);
                }
                else
                {
                    MXS_ERROR("Failed to execute session "
                              "command in %s:%d",
                              (*it)->backend()->server->address,
                              (*it)->backend()->server->port);
                }
            }
            else
            {
                mxb_assert((*it)->session_command_count() > 1);
                /** The server is already executing a session command */
                MXS_INFO("Backend %s:%d already executing sescmd.",
                         (*it)->backend()->server->address,
                         (*it)->backend()->server->port);
                succp = true;
            }
        }
    }

    gwbuf_free(querybuf);
    return succp;
}

/**
 * Check if a router session has servers in use
 * @param rses Router client session
 * @return True if session has a single backend server in use that is running.
 * False if no backends are in use or running.
 */
bool SchemaRouterSession::have_servers()
{
    for (SSRBackendList::iterator it = m_backends.begin(); it != m_backends.end(); it++)
    {
        if ((*it)->in_use() && !(*it)->is_closed())
        {
            return true;
        }
    }

    return false;
}

/**
 * Finds out if there is a backend reference pointing at the DCB given as
 * parameter.
 * @param rses  router client session
 * @param dcb   DCB
 *
 * @return backend reference pointer if succeed or NULL
 */
SSRBackend SchemaRouterSession::get_bref_from_dcb(DCB* dcb)
{
    for (SSRBackendList::iterator it = m_backends.begin(); it != m_backends.end(); it++)
    {
        if ((*it)->dcb() == dcb)
        {
            return *it;
        }
    }

    // This should not happen
    mxb_assert(false);
    return SSRBackend(reinterpret_cast<SRBackend*>(NULL));
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
        set->add_row({a.first, a.second->name});
    }

    set->write(m_client);

    return true;
}

/**
 *
 * @param dcb
 * @param errnum
 * @param mysqlstate
 * @param errmsg
 */
void write_error_to_client(DCB* dcb, int errnum, const char* mysqlstate, const char* errmsg)
{
    GWBUF* errbuff = modutil_create_mysql_err_msg(1, 0, errnum, mysqlstate, errmsg);
    if (errbuff)
    {
        if (dcb->func.write(dcb, errbuff) != 1)
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
    SERVER* target = m_shard.get_location(m_connect_db);

    if (target)
    {
        /* Send a COM_INIT_DB packet to the server with the right database
         * and set it as the client's active database */

        unsigned int qlen = m_connect_db.length();
        GWBUF* buffer = gwbuf_alloc(qlen + 5);

        if (buffer)
        {
            uint8_t* data = GWBUF_DATA(buffer);
            gw_mysql_set_byte3(data, qlen + 1);
            data[3] = 0x0;
            data[4] = 0x2;
            memcpy(data + 5, m_connect_db.c_str(), qlen);
            SSRBackend backend;
            DCB* dcb = NULL;

            if (get_shard_dcb(&dcb, target->name)
                && (backend = get_bref_from_dcb(dcb)))
            {
                backend->write(buffer);
                MXS_DEBUG("USE '%s' sent to %s for session %p",
                          m_connect_db.c_str(),
                          target->name,
                          m_client->session);
                rval = true;
            }
            else
            {
                MXS_INFO("Couldn't find target DCB for '%s'.", target->name);
            }
        }
        else
        {
            MXS_ERROR("Buffer allocation failed.");
        }
    }
    else
    {
        /** Unknown database, hang up on the client*/
        MXS_INFO("Connecting to a non-existent database '%s'", m_connect_db.c_str());
        char errmsg[128 + MYSQL_DATABASE_MAXLEN + 1];
        sprintf(errmsg, "Unknown database '%s'", m_connect_db.c_str());
        if (m_config->debug)
        {
            sprintf(errmsg + strlen(errmsg),
                    " ([%" PRIu64 "]: DB not found on connect)",
                    m_client->session->ses_id);
        }
        write_error_to_client(m_client,
                              SCHEMA_ERR_DBNOTFOUND,
                              SCHEMA_ERRSTR_DBNOTFOUND,
                              errmsg);
    }

    return rval;
}

void SchemaRouterSession::route_queued_query()
{
    GWBUF* tmp = m_queue.front().release();
    m_queue.pop_front();

#ifdef SS_DEBUG
    char* querystr = modutil_get_SQL(tmp);
    MXS_DEBUG("Sending queued buffer for session %p: %s",
              m_client->session,
              querystr);
    MXS_FREE(querystr);
#endif

    poll_add_epollin_event_to_dcb(m_client, tmp);
}

/**
 *
 * @param router_cli_ses Router client session
 * @return 1 if mapping is done, 0 if it is still ongoing and -1 on error
 */
int SchemaRouterSession::inspect_mapping_states(SSRBackend& bref,
                                                GWBUF** wbuf)
{
    bool mapped = true;
    GWBUF* writebuf = *wbuf;

    for (SSRBackendList::iterator it = m_backends.begin(); it != m_backends.end(); it++)
    {
        if (bref->dcb() == (*it)->dcb() && !(*it)->is_mapped())
        {
            enum showdb_response rc = parse_mapping_response(*it, &writebuf);

            if (rc == SHOWDB_FULL_RESPONSE)
            {
                (*it)->set_mapped(true);
                MXS_DEBUG("Received SHOW DATABASES reply from %s for session %p",
                          (*it)->backend()->server->name,
                          m_client->session);
            }
            else
            {
                mxb_assert(rc != SHOWDB_PARTIAL_RESPONSE);
                DCB* client_dcb = NULL;

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
                    client_dcb = m_client;

                    /** This is the first response to the database mapping which
                     * has duplicate database conflict. Set the initialization bitmask
                     * to INIT_FAILED */
                    m_state |= INIT_FAILED;

                    /** Send the client an error about duplicate databases
                     * if there is a queued query from the client. */
                    if (m_queue.size())
                    {
                        GWBUF* error = modutil_create_mysql_err_msg(1,
                                                                    0,
                                                                    SCHEMA_ERR_DUPLICATEDB,
                                                                    SCHEMA_ERRSTR_DUPLICATEDB,
                                                                    "Error: duplicate tables "
                                                                    "found on two different shards.");

                        if (error)
                        {
                            client_dcb->func.write(client_dcb, error);
                        }
                        else
                        {
                            MXS_ERROR("Creating buffer for error message failed.");
                        }
                    }
                }
                *wbuf = writebuf;
                return -1;
            }
        }

        if ((*it)->in_use() && !(*it)->is_mapped())
        {
            mapped = false;
            MXS_DEBUG("Still waiting for reply to SHOW DATABASES from %s for session %p",
                      (*it)->backend()->server->name,
                      m_client->session);
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

    if (GWBUF_LENGTH(buf) <= MYSQL_DATABASE_MAXLEN - 5)
    {
        /** Copy database name from MySQL packet to session */
        if (extract_database(buf, db))
        {
            MXS_INFO("change_current_db: INIT_DB with database '%s'", db);
            /**
             * Update the session's active database only if it's in the hashtable.
             * If it isn't found, send a custom error packet to the client.
             */

            SERVER* target = shard.get_location(db);

            if (target)
            {
                dest = db;
                MXS_INFO("change_current_db: database is on server: '%s'.", target->name);
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
 * Convert a length encoded string into a C string.
 * @param data Pointer to the first byte of the string
 * @return Pointer to the newly allocated string or NULL if the value is NULL or an error occurred
 */
char* get_lenenc_str(void* data)
{
    unsigned char* ptr = (unsigned char*)data;
    char* rval;
    uintptr_t size;
    long offset;

    if (data == NULL)
    {
        return NULL;
    }

    if (*ptr < 251)
    {
        size = (uintptr_t) * ptr;
        offset = 1;
    }
    else
    {
        switch (*(ptr))
        {
        case 0xfb:
            return NULL;

        case 0xfc:
            size = *(ptr + 1) + (*(ptr + 2) << 8);
            offset = 2;
            break;

        case 0xfd:
            size = *ptr + (*(ptr + 2) << 8) + (*(ptr + 3) << 16);
            offset = 3;
            break;

        case 0xfe:
            size = *ptr + ((*(ptr + 2) << 8)) + (*(ptr + 3) << 16)
                + (*(ptr + 4) << 24) + ((uintptr_t) * (ptr + 5) << 32)
                + ((uintptr_t) * (ptr + 6) << 40)
                + ((uintptr_t) * (ptr + 7) << 48) + ((uintptr_t) * (ptr + 8) << 56);
            offset = 8;
            break;

        default:
            return NULL;
        }
    }

    rval = (char*)MXS_MALLOC(sizeof(char) * (size + 1));
    if (rval)
    {
        memcpy(rval, ptr + offset, size);
        memset(rval + size, 0, 1);
    }
    return rval;
}

bool SchemaRouterSession::ignore_duplicate_database(const char* data)
{
    bool rval = false;

    if (m_config->ignored_dbs.find(data) != m_config->ignored_dbs.end())
    {
        rval = true;
    }
    else if (m_config->ignore_regex)
    {
        pcre2_match_data* match_data = pcre2_match_data_create_from_pattern(m_config->ignore_regex, NULL);

        if (match_data == NULL)
        {
            throw std::bad_alloc();
        }

        if (pcre2_match(m_config->ignore_regex,
                        (PCRE2_SPTR) data,
                        PCRE2_ZERO_TERMINATED,
                        0,
                        0,
                        match_data,
                        NULL) >= 0)
        {
            rval = true;
        }

        pcre2_match_data_free(match_data);
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
enum showdb_response SchemaRouterSession::parse_mapping_response(SSRBackend& bref, GWBUF** buffer)
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
        MXS_INFO("Mapping query returned an error.");
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
            *buffer = gwbuf_append(buf, *buffer);
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
        char* data = get_lenenc_str(ptr + 4);
        SERVER* target = bref->backend()->server;

        if (data)
        {
            if (m_shard.add_location(data, target))
            {
                MXS_INFO("<%s, %s>", target->name, data);
            }
            else
            {
                if (!ignore_duplicate_database(data) && strchr(data, '.') != NULL)
                {
                    duplicate_found = true;
                    SERVER* duplicate = m_shard.get_location(data);

                    MXS_ERROR("Table '%s' found on servers '%s' and '%s' for user %s@%s.",
                              data,
                              target->name,
                              duplicate->name,
                              m_client->user,
                              m_client->remote);
                }
                else if (m_config->preferred_server == target)
                {
                    /** In conflict situations, use the preferred server */
                    MXS_INFO("Forcing location of '%s' from '%s' to '%s'",
                             data,
                             m_shard.get_location(data)->name,
                             target->name);
                    m_shard.replace_location(data, target);
                }
            }
            MXS_FREE(data);
        }
        ptr += packetlen;
    }

    if (ptr < (unsigned char*) buf->end && PTR_IS_EOF(ptr) && n_eof == 1)
    {
        n_eof++;
        MXS_INFO("SHOW DATABASES fully received from %s.",
                 bref->backend()->server->name);
    }
    else
    {
        MXS_INFO("SHOW DATABASES partially received from %s.",
                 bref->backend()->server->name);
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

    for (SSRBackendList::iterator it = m_backends.begin(); it != m_backends.end(); it++)
    {
        (*it)->set_mapped(false);
    }

    m_state |= INIT_MAPPING;
    m_state &= ~INIT_UNINT;

    GWBUF* buffer = modutil_create_query("SELECT schema_name FROM information_schema.schemata AS s "
                                         "LEFT JOIN information_schema.tables AS t ON s.schema_name = t.table_schema "
                                         "WHERE t.table_name IS NULL "
                                         "UNION "
                                         "SELECT CONCAT (table_schema, '.', table_name) FROM information_schema.tables "
                                         "WHERE table_schema NOT IN ('information_schema', 'performance_schema', 'mysql');");
    gwbuf_set_type(buffer, GWBUF_TYPE_COLLECT_RESULT);

    for (SSRBackendList::iterator it = m_backends.begin(); it != m_backends.end(); it++)
    {
        if ((*it)->in_use() && !(*it)->is_closed() && server_is_usable((*it)->backend()->server))
        {
            GWBUF* clone = gwbuf_clone(buffer);
            MXS_ABORT_IF_NULL(clone);

            if (!(*it)->write(clone))
            {
                MXS_ERROR("Failed to write mapping query to '%s'",
                          (*it)->backend()->server->name);
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
SERVER* SchemaRouterSession::get_shard_target(GWBUF* buffer, uint32_t qtype)
{
    SERVER* rval = NULL;
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
        for (SSRBackendList::iterator it = m_backends.begin(); it != m_backends.end(); it++)
        {
            char* srvnm = (*it)->backend()->server->name;

            if (strcmp(srvnm, (char*)buffer->hint->data) == 0)
            {
                rval = (*it)->backend()->server;
                MXS_INFO("Routing hint found (%s)", rval->name);
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
                     rval->name);
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
bool SchemaRouterSession::get_shard_dcb(DCB** p_dcb, char* name)
{
    bool succp = false;
    mxb_assert(p_dcb != NULL && *(p_dcb) == NULL);

    for (SSRBackendList::iterator it = m_backends.begin(); it != m_backends.end(); it++)
    {
        SERVER_REF* b = (*it)->backend();
        /**
         * To become chosen:
         * backend must be in use, name must match, and
         * the backend state must be RUNNING
         */
        if ((*it)->in_use()
            && (strncasecmp(name, b->server->name, PATH_MAX) == 0)
            && server_is_usable(b->server))
        {
            *p_dcb = (*it)->dcb();
            succp = true;
            break;
        }
    }

    return succp;
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
    std::list<std::string> db_names;
    m_shard.get_content(dblist);
    for (ServerMap::iterator it = dblist.begin(); it != dblist.end(); it++)
    {
        std::string db = it->first.substr(0, it->first.find("."));
        if (std::find(db_names.begin(), db_names.end(), db) == db_names.end())
        {
            db_names.push_back(db);
        }
    }

    std::unique_ptr<ResultSet> set = ResultSet::create({"Database"});

    for (const auto& name : db_names)
    {
        set->add_row({name});
    }

    set->write(m_client);
}

bool SchemaRouterSession::send_tables(GWBUF* pPacket)
{
    char* query = modutil_get_SQL(pPacket);
    char* tmp;
    std::string database;

    if ((tmp = strcasestr(query, "from")))
    {
        const char* delim = "` \n\t;";
        char* saved, * tok = strtok_r(tmp, delim, &saved);
        tok = strtok_r(NULL, delim, &saved);
        database = tok;
    }

    if (database.empty())
    {
        MXS_FREE(query);
        return false;
    }

    ServerMap tablelist;
    std::list<std::string> table_names;
    m_shard.get_content(tablelist);

    for (ServerMap::iterator it = tablelist.begin(); it != tablelist.end(); it++)
    {
        std::size_t pos = it->first.find(".");
        // If the database is empty ignore it
        if (pos == std::string::npos)
        {
            continue;
        }
        std::string db = it->first.substr(0, pos);

        if (db.compare(database) == 0)
        {
            std::string table = it->first.substr(pos + 1);
            table_names.push_back(table);
        }
    }

    std::unique_ptr<ResultSet> set = ResultSet::create({"Table"});

    for (const auto& name : table_names)
    {
        set->add_row({name});
    }

    set->write(m_client);

    MXS_FREE(query);
    return true;
}

bool SchemaRouterSession::handle_statement(GWBUF* querybuf, SSRBackend& bref, uint8_t command, uint32_t type)
{
    bool succp = false;

    mxb::atomic::add(&m_stats.longest_sescmd, 1, mxb::atomic::RELAXED);

    /** Increment the session command count */
    ++m_sent_sescmd;

    if (bref->in_use())
    {
        GWBUF* buffer = gwbuf_clone(querybuf);
        bref->append_session_command(buffer, m_sent_sescmd);

        if (bref->session_command_count() == 1)
        {
            if (bref->execute_session_command())
            {
                succp = true;
                mxb::atomic::add(&bref->server()->stats.packets, 1, mxb::atomic::RELAXED);
            }
            else
            {
                MXS_ERROR("Failed to execute session "
                          "command in %s:%d",
                          bref->backend()->server->address,
                          bref->backend()->server->port);
            }
        }
        else
        {
            mxb_assert(bref->session_command_count() > 1);
            /** The server is already executing a session command */
            MXS_INFO("Backend %s:%d already executing sescmd.",
                     bref->backend()->server->address,
                     bref->backend()->server->port);
            succp = true;
        }
    }

    gwbuf_free(querybuf);
    return succp;
}

SERVER* SchemaRouterSession::get_query_target(GWBUF* buffer)
{
    int n_tables = 0;
    char** tables = qc_get_table_names(buffer, &n_tables, true);
    SERVER* rval = NULL;

    for (int i = 0; i < n_tables; i++)
    {
        if (strchr(tables[i], '.') == NULL)
        {
            rval = m_shard.get_location(m_current_db);
            break;
        }
    }

    int n_databases = 0;
    char** databases = qc_get_database_names(buffer, &n_databases);

    for (int i = 0; i < n_databases; i++)
    {
        for (int j = 0; j < n_tables; j++)
        {
            SERVER* target = m_shard.get_location(tables[j]);

            if (target)
            {

                if (rval && target != rval)
                {
                    MXS_ERROR("Query targets tables on servers '%s' and '%s'. "
                              "Cross server queries are not supported.",
                              rval->name,
                              target->name);
                }
                else if (rval == NULL)
                {
                    rval = target;
                    MXS_INFO("Query targets table '%s' on server '%s'",
                             tables[j],
                             rval->name);
                }
            }
        }

        MXS_FREE(databases[i]);
    }

    for (int i = 0; i < n_tables; i++)
    {
        MXS_FREE(tables[i]);
    }
    MXS_FREE(tables);
    MXS_FREE(databases);
    return rval;
}

SERVER* SchemaRouterSession::get_ps_target(GWBUF* buffer, uint32_t qtype, qc_query_op_t op)
{
    SERVER* rval = NULL;
    uint8_t command = mxs_mysql_get_command(buffer);

    if (qc_query_is_type(qtype, QUERY_TYPE_PREPARE_NAMED_STMT))
    {
        GWBUF* pStmt = qc_get_preparable_stmt(buffer);
        int n_tables = 0;
        char** tables = qc_get_table_names(pStmt, &n_tables, true);
        char* stmt = qc_get_prepare_name(buffer);

        for (int i = 0; i < n_tables; i++)
        {
            SERVER* target = m_shard.get_location(tables[i]);

            if (target)
            {

                if (rval && target != rval)
                {
                    MXS_ERROR("Statement targets tables on servers '%s' and '%s'. "
                              "Cross server queries are not supported.",
                              rval->name,
                              target->name);
                }
                else if (rval == NULL)
                {
                    rval = target;
                }
            }
            MXS_FREE(tables[i]);
        }

        if (rval)
        {
            MXS_INFO("PREPARING NAMED %s ON SERVER %s", stmt, rval->name);
            m_shard.add_statement(stmt, rval);
        }
        MXS_FREE(tables);
        MXS_FREE(stmt);
    }
    else if (op == QUERY_OP_EXECUTE)
    {
        char* stmt = qc_get_prepare_name(buffer);
        rval = m_shard.get_statement(stmt);
        MXS_INFO("Executing named statement %s on server %s", stmt, rval->name);
        MXS_FREE(stmt);
    }
    else if (qc_query_is_type(qtype, QUERY_TYPE_DEALLOC_PREPARE))
    {
        char* stmt = qc_get_prepare_name(buffer);

        if ((rval = m_shard.get_statement(stmt)))
        {
            MXS_INFO("Closing named statement %s on server %s", stmt, rval->name);
            m_shard.remove_statement(stmt);
        }
        MXS_FREE(stmt);
    }
    else if (qc_query_is_type(qtype, QUERY_TYPE_PREPARE_STMT))
    {
        int n_tables = 0;
        char** tables = qc_get_table_names(buffer, &n_tables, true);

        for (int i = 0; i < n_tables; i++)
        {
            rval = m_shard.get_location(tables[0]);
            MXS_FREE(tables[i]);
        }
        rval ? MXS_INFO("Prepare statement on server %s", rval->name) :
        MXS_INFO("Prepared statement targets no mapped tables");
        MXS_FREE(tables);
    }
    else if (mxs_mysql_is_ps_command(command))
    {
        uint32_t id = mxs_mysql_extract_ps_id(buffer);
        uint32_t handle = m_shard.get_ps_handle(id);
        uint8_t* ptr = GWBUF_DATA(buffer) + MYSQL_PS_ID_OFFSET;
        gw_mysql_set_byte4(ptr, handle);
        rval = m_shard.get_statement(id);

        if (command == MXS_COM_STMT_CLOSE)
        {
            MXS_INFO("Closing prepared statement %d ", id);
            m_shard.remove_statement(id);
        }
    }
    return rval;
}
}
