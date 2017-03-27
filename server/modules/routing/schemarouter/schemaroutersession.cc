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

#include "schemarouter.hh"

#include <maxscale/alloc.h>
#include <maxscale/query_classifier.h>
#include <maxscale/modutil.h>

#include "schemaroutersession.hh"
#include "schemarouterinstance.hh"

bool connect_backend_servers(backend_ref_t* backend_ref,
                             int router_nservers,
                             MXS_SESSION* session);

route_target_t get_shard_route_target(uint32_t qtype);
bool execute_sescmd_in_backend(backend_ref_t* backend_ref);

void bref_clear_state(backend_ref_t* bref, bref_state_t state);
void bref_set_state(backend_ref_t* bref, bref_state_t state);

bool change_current_db(string& dest, Shard& shard, GWBUF* buf);
bool extract_database(GWBUF* buf, char* str);
bool detect_show_shards(GWBUF* query);
void write_error_to_client(DCB* dcb, int errnum, const char* mysqlstate, const char* errmsg);


SchemaRouterSession::SchemaRouterSession(MXS_SESSION* session, SchemaRouter& router):
    mxs::RouterSession(session),
    m_router(router)
{
    char db[MYSQL_DATABASE_MAXLEN + 1] = "";
    MySQLProtocol* protocol = (MySQLProtocol*)session->client_dcb->protocol;
    MYSQL_session* data = (MYSQL_session*)session->client_dcb->data;
    bool using_db = false;
    bool have_db = false;

    /* To enable connecting directly to a sharded database we first need
     * to disable it for the client DCB's protocol so that we can connect to them*/
    if (protocol->client_capabilities & GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB &&
        (have_db = strnlen(data->db, MYSQL_DATABASE_MAXLEN) > 0))
    {
        protocol->client_capabilities &= ~GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB;
        strcpy(db, data->db);
        *data->db = 0;
        using_db = true;
        MXS_INFO("Client logging in directly to a database '%s', "
                 "postponing until databases have been mapped.", db);
    }

    if (!have_db)
    {
        MXS_INFO("Client'%s' connecting with empty database.", data->user);
    }

    SchemaRouterSession& client_rses = *this;

    this->m_router = router;
    this->m_client = (DCB*)session->client_dcb;
    this->m_queue = NULL;
    this->m_closed = false;
    this->m_sent_sescmd = 0;
    this->m_replied_sescmd = 0;

    this->m_shard = router.m_shard_manager.get_shard(session->client_dcb->user,
                                                     router.m_config.refresh_min_interval);

    this->m_config = router.m_config;

    if (using_db)
    {
        this->m_state |= INIT_USE_DB;
    }
    /**
     * Set defaults to session variables.
     */

    /**
     * Instead of calling this, ensure that there is at least one
     * responding server.
     */

    int router_nservers = router.m_service->n_dbref;

    /**
     * Create backend reference objects for this session.
     */

    backend_ref_t* backend_ref = new backend_ref_t[router_nservers];

    /**
     * Initialize backend references with BACKEND ptr.
     * Initialize session command cursors for each backend reference.
     */

    int i = 0;

    for (SERVER_REF *ref = router.m_service->dbref; ref; ref = ref->next)
    {
        if (ref->active)
        {
            backend_ref[i].state = 0;
            backend_ref[i].n_mapping_eof = 0;
            backend_ref[i].map_queue = NULL;
            backend_ref[i].backend = ref;
            backend_ref[i].pending_cmd = NULL;
            i++;
        }
    }

    if (i < router_nservers)
    {
        router_nservers = i;
    }

    this->m_backends = backend_ref;
    this->m_backend_count = router_nservers;

    /**
     * Connect to all backend servers
     */
    bool succp = connect_backend_servers(backend_ref, router_nservers, session);

    if (!succp)
    {
        throw std::runtime_error("Failed to connect to backend servers");
    }

    if (db[0])
    {
        /* Store the database the client is connecting to */
        this->m_connect_db = db;
    }

    atomic_add(&router.m_stats.sessions, 1);
}

SchemaRouterSession::~SchemaRouterSession()
{
    for (int i = 0; i < this->m_backend_count; i++)
    {
        gwbuf_free(this->m_backends[i].pending_cmd);
    }

    delete[] this->m_backends;
}

void SchemaRouterSession::close()
{
    ss_dassert(!this->m_closed);

    /**
     * Lock router client session for secure read and update.
     */
    if (!this->m_closed)
    {
        this->m_closed = true;

        for (int i = 0; i < this->m_backend_count; i++)
        {
            backend_ref_t* bref = &this->m_backends[i];
            DCB* dcb = bref->dcb;
            /** Close those which had been connected */
            if (BREF_IS_IN_USE(bref))
            {
                CHK_DCB(dcb);

                /** Clean operation counter in bref and in SERVER */
                while (BREF_IS_WAITING_RESULT(bref))
                {
                    bref_clear_state(bref, BREF_WAITING_RESULT);
                }
                bref_clear_state(bref, BREF_IN_USE);
                bref_set_state(bref, BREF_CLOSED);
                /**
                 * closes protocol and dcb
                 */
                dcb_close(dcb);
                /** decrease server current connection counters */
                atomic_add(&bref->backend->connections, -1);
            }
        }

        gwbuf_free(this->m_queue);

        spinlock_acquire(&m_router.m_lock);
        if (m_router.m_stats.longest_sescmd < this->m_stats.longest_sescmd)
        {
            m_router.m_stats.longest_sescmd = this->m_stats.longest_sescmd;
        }
        double ses_time = difftime(time(NULL), this->m_client->session->stats.connect);
        if (m_router.m_stats.ses_longest < ses_time)
        {
            m_router.m_stats.ses_longest = ses_time;
        }
        if (m_router.m_stats.ses_shortest > ses_time && m_router.m_stats.ses_shortest > 0)
        {
            m_router.m_stats.ses_shortest = ses_time;
        }

        m_router.m_stats.ses_average =
            (ses_time + ((m_router.m_stats.sessions - 1) * m_router.m_stats.ses_average)) /
            (m_router.m_stats.sessions);

        spinlock_release(&m_router.m_lock);
    }
}

int32_t SchemaRouterSession::routeQuery(GWBUF* pPacket)
{
    uint32_t qtype = QUERY_TYPE_UNKNOWN;
    uint8_t packet_type;
    uint8_t* packet;
    int ret = 0;
    DCB* target_dcb = NULL;
    bool change_successful = false;
    route_target_t route_target = TARGET_UNDEFINED;
    bool succp = false;
    char db[MYSQL_DATABASE_MAXLEN + 1];
    char errbuf[26 + MYSQL_DATABASE_MAXLEN];

    SERVER* target = NULL;

    ss_dassert(!GWBUF_IS_TYPE_UNDEFINED(pPacket));

    if (this->m_closed)
    {
        return 0;
    }

    if (this->m_shard.empty())
    {
        /* Generate database list */
        gen_databaselist();
    }

    /**
     * If the databases are still being mapped or if the client connected
     * with a default database but no database mapping was performed we need
     * to store the query. Once the databases have been mapped and/or the
     * default database is taken into use we can send the query forward.
     */
    if (this->m_state & (INIT_MAPPING | INIT_USE_DB))
    {
        int init_rval = 1;
        char* querystr = modutil_get_SQL(pPacket);
        MXS_INFO("Storing query for session %p: %s",
                 this->m_client->session,
                 querystr);
        MXS_FREE(querystr);
        pPacket = gwbuf_make_contiguous(pPacket);
        GWBUF* ptr = this->m_queue;

        while (ptr && ptr->next)
        {
            ptr = ptr->next;
        }

        if (ptr == NULL)
        {
            this->m_queue = pPacket;
        }
        else
        {
            ptr->next = pPacket;

        }

        if (this->m_state  == (INIT_READY | INIT_USE_DB))
        {
            /**
             * This state is possible if a client connects with a default database
             * and the shard map was found from the router cache
             */
            if (!handle_default_db())
            {
                init_rval = 0;
            }
        }

        return init_rval;
    }

    packet = GWBUF_DATA(pPacket);
    packet_type = packet[4];
    qc_query_op_t op = QUERY_OP_UNDEFINED;

    if (detect_show_shards(pPacket))
    {
        process_show_shards();
        gwbuf_free(pPacket);
        return 1;
    }

    switch (packet_type)
    {
    case MYSQL_COM_QUIT:        /*< 1 QUIT will close all sessions */
    case MYSQL_COM_INIT_DB:     /*< 2 DDL must go to the master */
    case MYSQL_COM_REFRESH:     /*< 7 - I guess this is session but not sure */
    case MYSQL_COM_DEBUG:       /*< 0d all servers dump debug info to stdout */
    case MYSQL_COM_PING:        /*< 0e all servers are pinged */
    case MYSQL_COM_CHANGE_USER: /*< 11 all servers change it accordingly */
    case MYSQL_COM_STMT_CLOSE:  /*< free prepared statement */
    case MYSQL_COM_STMT_SEND_LONG_DATA: /*< send data to column */
    case MYSQL_COM_STMT_RESET:  /*< resets the data of a prepared statement */
        qtype = QUERY_TYPE_SESSION_WRITE;
        break;

    case MYSQL_COM_CREATE_DB:   /**< 5 DDL must go to the master */
    case MYSQL_COM_DROP_DB:     /**< 6 DDL must go to the master */
        qtype = QUERY_TYPE_WRITE;
        break;

    case MYSQL_COM_QUERY:
        qtype = qc_get_type_mask(pPacket);
        op = qc_get_operation(pPacket);
        break;

    case MYSQL_COM_STMT_PREPARE:
        qtype = qc_get_type_mask(pPacket);
        qtype |= QUERY_TYPE_PREPARE_STMT;
        break;

    case MYSQL_COM_STMT_EXECUTE:
        /** Parsing is not needed for this type of packet */
        qtype = QUERY_TYPE_EXEC_STMT;
        break;

    case MYSQL_COM_SHUTDOWN:       /**< 8 where should shutdown be routed ? */
    case MYSQL_COM_STATISTICS:     /**< 9 ? */
    case MYSQL_COM_PROCESS_INFO:   /**< 0a ? */
    case MYSQL_COM_CONNECT:        /**< 0b ? */
    case MYSQL_COM_PROCESS_KILL:   /**< 0c ? */
    case MYSQL_COM_TIME:           /**< 0f should this be run in gateway ? */
    case MYSQL_COM_DELAYED_INSERT: /**< 10 ? */
    case MYSQL_COM_DAEMON:         /**< 1d ? */
    default:
        break;
    }

    if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
    {
        char *sql;
        int sql_len;
        char* qtypestr = qc_typemask_to_string(qtype);
        modutil_extract_SQL(pPacket, &sql, &sql_len);

        MXS_INFO("> Command: %s, stmt: %.*s %s%s",
                 STRPACKETTYPE(packet_type), sql_len, sql,
                 (pPacket->hint == NULL ? "" : ", Hint:"),
                 (pPacket->hint == NULL ? "" : STRHINTTYPE(pPacket->hint->type)));

        MXS_FREE(qtypestr);
    }
    /**
     * Find out whether the query should be routed to single server or to
     * all of them.
     */

    if (packet_type == MYSQL_COM_INIT_DB || op == QUERY_OP_CHANGE_DB)
    {
        change_successful = change_current_db(this->m_current_db,
                                              this->m_shard,
                                              pPacket);
        if (!change_successful)
        {
            extract_database(pPacket, db);
            snprintf(errbuf, 25 + MYSQL_DATABASE_MAXLEN, "Unknown database: %s", db);

            if (this->m_config.debug)
            {
                sprintf(errbuf + strlen(errbuf),
                        " ([%lu]: DB change failed)",
                        this->m_client->session->ses_id);
            }

            write_error_to_client(this->m_client,
                                  SCHEMA_ERR_DBNOTFOUND,
                                  SCHEMA_ERRSTR_DBNOTFOUND,
                                  errbuf);

            MXS_ERROR("Changing database failed.");
            gwbuf_free(pPacket);
            return 1;
        }
    }

    /** Create the response to the SHOW DATABASES from the mapped databases */
    if (qc_query_is_type(qtype, QUERY_TYPE_SHOW_DATABASES))
    {
        if (send_database_list())
        {
            ret = 1;
        }

        gwbuf_free(pPacket);
        return ret;
    }

    route_target = get_shard_route_target(qtype);

    if (packet_type == MYSQL_COM_INIT_DB || op == QUERY_OP_CHANGE_DB)
    {
        route_target = TARGET_UNDEFINED;
        target = this->m_shard.get_location(this->m_current_db);

        if (target)
        {
            MXS_INFO("INIT_DB for database '%s' on server '%s'",
                     this->m_current_db.c_str(), target->unique_name);
            route_target = TARGET_NAMED_SERVER;
        }
        else
        {
            MXS_INFO("INIT_DB with unknown database");
        }
    }
    else if (route_target != TARGET_ALL)
    {
        /** If no database is found in the query and there is no active database
         * or hints in the query we route the query to the first available
         * server. This isn't ideal for monitoring server status but works if
         * we just want the server to send an error back. */

        target = get_shard_target(pPacket, qtype);

        if (target)
        {
            if (SERVER_IS_RUNNING(target))
            {
                route_target = TARGET_NAMED_SERVER;
            }
            else
            {
                MXS_INFO("Backend server '%s' is not in a viable state", target->unique_name);

                /**
                 * Shard is not a viable target right now so we check
                 * for an alternate backend with the database. If this is not found
                 * the target is undefined and an error will be returned to the client.
                 */
            }
        }
    }

    if (TARGET_IS_UNDEFINED(route_target))
    {
        target = get_shard_target(pPacket, qtype);

        if ((target == NULL &&
             packet_type != MYSQL_COM_INIT_DB &&
             this->m_current_db.length() == 0) ||
            packet_type == MYSQL_COM_FIELD_LIST ||
            (this->m_current_db.length() == 0))
        {
            /**
             * No current database and no databases in query or
             * the database is ignored, route to first available backend.
             */

            route_target = TARGET_ANY;
            MXS_INFO("Routing query to first available backend.");

        }
        else
        {
            if (!change_successful)
            {
                /**
                 * Bad shard status. The changing of the database
                 * was not successful and the error message was already sent.
                 */

                ret = 1;
            }
            else
            {
                MXS_ERROR("Error : Router internal failure (schemarouter)");
                /** Something else went wrong, terminate connection */
                ret = 0;
            }

            gwbuf_free(pPacket);
            return ret;
        }
    }

    if (TARGET_IS_ALL(route_target))
    {
        /**
         * It is not sure if the session command in question requires
         * response. Statement is examined in route_session_write.
         * Router locking is done inside the function.
         */
        succp = route_session_write(pPacket, packet_type);

        if (succp)
        {
            atomic_add(&m_router.m_stats.n_sescmd, 1);
            atomic_add(&m_router.m_stats.n_queries, 1);
            ret = 1;
        }

        gwbuf_free(pPacket);
        return ret;
    }

    if (TARGET_IS_ANY(route_target))
    {
        for (int i = 0; i < this->m_backend_count; i++)
        {
            SERVER *server = this->m_backends[i].backend->server;
            if (SERVER_IS_RUNNING(server))
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
            gwbuf_free(pPacket);
            return 0;
        }

    }

    /**
     * Query is routed to one of the backends
     */
    if (TARGET_IS_NAMED_SERVER(route_target) && target)
    {
        /**
         * Search backend server by name or replication lag.
         * If it fails, then try to find valid slave or master.
         */

        succp = get_shard_dcb(&target_dcb, target->unique_name);

        if (!succp)
        {
            MXS_INFO("Was supposed to route to named server "
                     "%s but couldn't find the server in a "
                     "suitable state.", target->unique_name);
        }

    }

    if (succp) /*< Have DCB of the target backend */
    {
        backend_ref_t *bref = get_bref_from_dcb(target_dcb);

        MXS_INFO("Route query to \t%s:%d <",
                 bref->backend->server->name,
                 bref->backend->server->port);
        /**
         * Store current stmt if execution of previous session command
         * haven't completed yet. Note that according to MySQL protocol
         * there can only be one such non-sescmd stmt at the time.
         */
        if (bref->session_commands.size() > 0)
        {
            ss_dassert((bref->pending_cmd == NULL ||
                        this->m_closed));
            bref->pending_cmd = pPacket;
            return 1;
        }

        if ((ret = target_dcb->func.write(target_dcb, gwbuf_clone(pPacket))) == 1)
        {
            backend_ref_t* bref;

            atomic_add(&m_router.m_stats.n_queries, 1);

            /**
             * Add one query response waiter to backend reference
             */
            bref = get_bref_from_dcb(target_dcb);
            bref_set_state(bref, BREF_QUERY_ACTIVE);
            bref_set_state(bref, BREF_WAITING_RESULT);
        }
        else
        {
            MXS_ERROR("Routing query failed.");
        }
    }

    gwbuf_free(pPacket);

    return ret;
}

void SchemaRouterSession::clientReply(GWBUF* pPacket, DCB* pDcb)
{
    backend_ref_t* bref;
    GWBUF* writebuf = pPacket;

    /**
     * Lock router client session for secure read of router session members.
     * Note that this could be done without lock by using version #
     */
    if (this->m_closed)
    {
        gwbuf_free(pPacket);
        return;
    }

    /** Holding lock ensures that router session remains open */
    ss_dassert(pDcb->session != NULL);
    DCB *client_dcb = pDcb->session->client_dcb;

    bref = get_bref_from_dcb(pDcb);

    if (bref == NULL)
    {
        gwbuf_free(writebuf);
        return;
    }

    MXS_DEBUG("Reply from [%s] session [%p]"
              " mapping [%s] queries queued [%s]",
              bref->backend->server->unique_name,
              this->m_client->session,
              this->m_state & INIT_MAPPING ? "true" : "false",
              this->m_queue == NULL ? "none" :
              this->m_queue->next ? "multiple" : "one");



    if (this->m_state & INIT_MAPPING)
    {
        int rc = inspect_backend_mapping_states(bref, &writebuf);
        gwbuf_free(writebuf);
        writebuf = NULL;

        if (rc == 1)
        {
            synchronize_shard_map();

            /*
             * Check if the session is reconnecting with a database name
             * that is not in the hashtable. If the database is not found
             * then close the session.
             */
            this->m_state &= ~INIT_MAPPING;

            if (this->m_state & INIT_USE_DB)
            {
                bool success = handle_default_db();
                if (!success)
                {
                    dcb_close(this->m_client);
                }
                return;
            }

            if (this->m_queue)
            {
                ss_dassert(this->m_state == INIT_READY);
                route_queued_query();
            }
        }

        if (rc == -1)
        {
            dcb_close(this->m_client);
        }
        return;
    }

    if (this->m_state & INIT_USE_DB)
    {
        MXS_DEBUG("Reply to USE '%s' received for session %p",
                  this->m_connect_db.c_str(),
                  this->m_client->session);
        this->m_state &= ~INIT_USE_DB;
        this->m_current_db = this->m_connect_db;
        ss_dassert(this->m_state == INIT_READY);

        if (this->m_queue)
        {
            route_queued_query();
        }

        gwbuf_free(writebuf);
        return;
    }

    if (this->m_queue)
    {
        ss_dassert(this->m_state == INIT_READY);
        route_queued_query();
        return;
    }



    /**
     * Active cursor means that reply is from session command
     * execution.
     */
    if (bref->session_commands.size() > 0)
    {
        if (GWBUF_IS_TYPE_SESCMD_RESPONSE(writebuf))
        {
            /**
             * Discard all those responses that have already been sent to
             * the client. Return with buffer including response that
             * needs to be sent to client or NULL.
             */
            if (this->m_replied_sescmd < this->m_sent_sescmd &&
                bref->session_commands.front().get_position() == this->m_replied_sescmd + 1)
            {
                ++this->m_replied_sescmd;
            }
            else
            {
                /** The reply to this session command has already been sent
                 * to the client. */
                gwbuf_free(writebuf);
                writebuf = NULL;
            }
            bref->session_commands.pop_front();
        }
        /**
         * If response will be sent to client, decrease waiter count.
         * This applies to session commands only. Counter decrement
         * for other type of queries is done outside this block.
         */
        if (writebuf != NULL && client_dcb != NULL)
        {
            /** Set response status as replied */
            bref_clear_state(bref, BREF_WAITING_RESULT);
        }
    }
    /**
     * Clear BREF_QUERY_ACTIVE flag and decrease waiter counter.
     * This applies for queries  other than session commands.
     */
    else if (BREF_IS_QUERY_ACTIVE(bref))
    {
        bref_clear_state(bref, BREF_QUERY_ACTIVE);
        /** Set response status as replied */
        bref_clear_state(bref, BREF_WAITING_RESULT);
    }

    if (writebuf != NULL && client_dcb != NULL)
    {
        unsigned char* cmd = (unsigned char*) writebuf->start;
        int state = this->m_state;
        /** Write reply to client DCB */
        MXS_INFO("returning reply [%s] "
                 "state [%s]  session [%p]",
                 PTR_IS_ERR(cmd) ? "ERR" : PTR_IS_OK(cmd) ? "OK" : "RSET",
                 state & INIT_UNINT ? "UNINIT" : state & INIT_MAPPING ? "MAPPING" : "READY",
                 this->m_client->session);
        MXS_SESSION_ROUTE_REPLY(pDcb->session, writebuf);
    }

    /** There is one pending session command to be executed. */
    if (bref->session_commands.size() > 0)
    {

        MXS_INFO("Backend %s:%d processed reply and starts to execute "
                 "active cursor.",
                 bref->backend->server->name,
                 bref->backend->server->port);

        execute_sescmd_in_backend(bref);
    }
    else if (bref->pending_cmd != NULL) /*< non-sescmd is waiting to be routed */
    {
        int ret;

        CHK_GWBUF(bref->pending_cmd);

        if ((ret = bref->dcb->func.write(bref->dcb, gwbuf_clone(bref->pending_cmd))) == 1)
        {
            atomic_add(&this->m_router.m_stats.n_queries, 1);
            /**
             * Add one query response waiter to backend reference
             */
            bref_set_state(bref, BREF_QUERY_ACTIVE);
            bref_set_state(bref, BREF_WAITING_RESULT);
        }
        else
        {
            char* sql = modutil_get_SQL(bref->pending_cmd);

            if (sql)
            {
                MXS_ERROR("Routing query \"%s\" failed.", sql);
                MXS_FREE(sql);
            }
            else
            {
                MXS_ERROR("Routing query failed.");
            }
        }
        gwbuf_free(bref->pending_cmd);
        bref->pending_cmd = NULL;
    }
}

void SchemaRouterSession::handleError(GWBUF* pMessage,
                                      DCB* pProblem,
                                      mxs_error_action_t action,
                                      bool* pSuccess)
{
    ss_dassert(pProblem->dcb_role == DCB_ROLE_BACKEND_HANDLER);
    CHK_DCB(pProblem);
    MXS_SESSION *session = pProblem->session;
    ss_dassert(session);

    CHK_SESSION(session);

    switch (action)
    {
    case ERRACT_NEW_CONNECTION:
        *pSuccess = handle_error_new_connection(pProblem, pMessage);
        break;

    case ERRACT_REPLY_CLIENT:
        handle_error_reply_client(pProblem, pMessage);
        *pSuccess = false; /*< no new backend servers were made available */
        break;

    default:
        *pSuccess = false;
        break;
    }

    dcb_close(pProblem);
}

/**
 * Private functions
 */


/**
 * Synchronize the router client session shard map with the global shard map for
 * this user.
 *
 * If the router doesn't have a shard map for this user then the current shard map
 * of the client session is added to the router. If the shard map in the router is
 * out of date, its contents are replaced with the contents of the current client
 * session. If the router has a usable shard map, the current shard map of the client
 * is discarded and the router's shard map is used.
 * @param client Router session
 */
void SchemaRouterSession::synchronize_shard_map()
{
    m_router.m_stats.shmap_cache_miss++;
    m_router.m_shard_manager.update_shard(this->m_shard, this->m_client->user);
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
    char *saved, *tok, *query = NULL;
    bool succp = true;
    unsigned int plen;

    packet = GWBUF_DATA(buf);
    plen = gw_mysql_get_byte3(packet) - 1;

    /** Copy database name from MySQL packet to session */
    if (qc_get_operation(buf) == QUERY_OP_CHANGE_DB)
    {
        const char *delim = "` \n\t;";

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
 * If session command cursor is passive, sends the command to backend for
 * execution.
 *
 * Returns true if command was sent or added successfully to the queue.
 * Returns false if command sending failed or if there are no pending session
 *      commands.
 *
 * Router session must be locked.
 */
bool SchemaRouterSession::execute_sescmd_in_backend(backend_ref_t* backend_ref)
{
    if (BREF_IS_CLOSED(backend_ref))
    {
        return false;
    }

    DCB *dcb = backend_ref->dcb;

    CHK_DCB(dcb);

    int rc = 0;

    /** Return if there are no pending ses commands */
    if (backend_ref->session_commands.size() == 0)
    {
        MXS_INFO("Cursor had no pending session commands.");
        return false;
    }

    SessionCommandList::iterator iter = backend_ref->session_commands.begin();
    GWBUF *buffer = iter->copy_buffer().release();

    switch (iter->get_command())
    {
    case MYSQL_COM_CHANGE_USER:
        /** This makes it possible to handle replies correctly */
        gwbuf_set_type(buffer, GWBUF_TYPE_SESCMD);
        rc = dcb->func.auth(dcb, NULL, dcb->session, buffer);
        break;

    case MYSQL_COM_QUERY:
    default:
        /**
         * Mark session command buffer, it triggers writing
         * MySQL command to protocol
         */
        gwbuf_set_type(buffer, GWBUF_TYPE_SESCMD);
        rc = dcb->func.write(dcb, buffer);
        break;
    }

    return rc == 1;
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
    backend_ref_t *backend_ref = this->m_backends;

    MXS_INFO("Session write, routing to all servers.");
    atomic_add(&this->m_stats.longest_sescmd, 1);

    /** Increment the session command count */
    ++this->m_sent_sescmd;

    for (int i = 0; i < this->m_backend_count; i++)
    {
        if (BREF_IS_IN_USE((&backend_ref[i])))
        {
            GWBUF *buffer = gwbuf_clone(querybuf);
            backend_ref[i].session_commands.push_back(SessionCommand(buffer, this->m_sent_sescmd));

            if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
            {
                MXS_INFO("Route query to %s\t%s:%d%s",
                         (SERVER_IS_MASTER(backend_ref[i].backend->server) ?
                          "master" : "slave"),
                         backend_ref[i].backend->server->name,
                         backend_ref[i].backend->server->port,
                         (i + 1 == this->m_backend_count ? " <" : ""));
            }

            if (backend_ref[i].session_commands.size() == 1)
            {
                /** Only one command, execute it */
                switch (command)
                {
                /** These types of commands don't generate responses */
                case MYSQL_COM_QUIT:
                case MYSQL_COM_STMT_CLOSE:
                    break;

                default:
                    bref_set_state(&backend_ref[i], BREF_WAITING_RESULT);
                    break;
                }

                if (execute_sescmd_in_backend(&backend_ref[i]))
                {
                    succp = true;
                }
                else
                {
                    MXS_ERROR("Failed to execute session "
                              "command in %s:%d",
                              backend_ref[i].backend->server->name,
                              backend_ref[i].backend->server->port);
                }
            }
            else
            {
                ss_dassert(backend_ref[i].session_commands.size() > 1);
                /** The server is already executing a session command */
                MXS_INFO("Backend %s:%d already executing sescmd.",
                         backend_ref[i].backend->server->name,
                         backend_ref[i].backend->server->port);
                succp = true;
            }
        }
    }

    return succp;
}

void SchemaRouterSession::handle_error_reply_client(DCB* dcb, GWBUF* errmsg)
{
    backend_ref_t*  bref = get_bref_from_dcb(dcb);

    if (bref)
    {

        bref_clear_state(bref, BREF_IN_USE);
        bref_set_state(bref, BREF_CLOSED);
    }

    if (dcb->session->state == SESSION_STATE_ROUTER_READY)
    {
        dcb->session->client_dcb->func.write(dcb->session->client_dcb, gwbuf_clone(errmsg));
    }
}

/**
 * Check if a router session has servers in use
 * @param rses Router client session
 * @return True if session has a single backend server in use that is running.
 * False if no backends are in use or running.
 */
bool SchemaRouterSession::have_servers()
{
    for (int i = 0; i < this->m_backend_count; i++)
    {
        if (BREF_IS_IN_USE(&this->m_backends[i]) &&
            !BREF_IS_CLOSED(&this->m_backends[i]))
        {
            return true;
        }
    }

    return false;
}

/**
 * Check if there is backend reference pointing at failed DCB, and reset its
 * flags. Then clear DCB's callback and finally try to reconnect.
 *
 * This must be called with router lock.
 *
 * @param inst          router instance
 * @param rses          router client session
 * @param dcb           failed DCB
 * @param errmsg        error message which is sent to client if it is waiting
 *
 * @return true if there are enough backend connections to continue, false if not
 */
bool SchemaRouterSession::handle_error_new_connection(DCB* backend_dcb, GWBUF* errmsg)
{
    backend_ref_t* bref;

    MXS_SESSION *ses = backend_dcb->session;
    CHK_SESSION(ses);

    /**
     * If bref == NULL it has been replaced already with another one.
     */
    if ((bref = get_bref_from_dcb(backend_dcb)) == NULL)
    {
        /** This should not happen */
        ss_dassert(false);
        return false;
    }

    /**
     * If query was sent through the bref and it is waiting for reply from
     * the backend server it is necessary to send an error to the client
     * because it is waiting for reply.
     */
    if (BREF_IS_WAITING_RESULT(bref))
    {
        DCB* client_dcb;
        client_dcb = ses->client_dcb;
        client_dcb->func.write(client_dcb, gwbuf_clone(errmsg));
        bref_clear_state(bref, BREF_WAITING_RESULT);
    }
    bref_clear_state(bref, BREF_IN_USE);
    bref_set_state(bref, BREF_CLOSED);

    return have_servers();
}

/**
 * Finds out if there is a backend reference pointing at the DCB given as
 * parameter.
 * @param rses  router client session
 * @param dcb   DCB
 *
 * @return backend reference pointer if succeed or NULL
 */
backend_ref_t* SchemaRouterSession::get_bref_from_dcb(DCB* dcb)
{
    CHK_DCB(dcb);

    for (int i = 0; i < this->m_backend_count; i++)
    {
        if (this->m_backends[i].dcb == dcb)
        {
            return &this->m_backends[i];
        }
    }

    return NULL;
}

/**
 * Detect if a query contains a SHOW SHARDS query.
 * @param query Query to inspect
 * @return true if the query is a SHOW SHARDS query otherwise false
 */
bool detect_show_shards(GWBUF* query)
{
    bool rval = false;
    char *querystr, *tok, *sptr;

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
 * Callback for the shard list result set creation
 */
RESULT_ROW* shard_list_cb(struct resultset* rset, void* data)
{
    ServerMap* pContent = (ServerMap*)data;
    RESULT_ROW* rval = resultset_make_row(rset);

    if (rval)
    {
        resultset_row_set(rval, 0, pContent->begin()->first.c_str());
        resultset_row_set(rval, 1, pContent->begin()->second->unique_name);
        pContent->erase(pContent->begin());
    }

    return rval;
}

/**
 * Send a result set of all shards and their locations to the client.
 * @param rses Router client session
 * @return 0 on success, -1 on error
 */
int SchemaRouterSession::process_show_shards()
{
    int rval = -1;

    ServerMap pContent;
    this->m_shard.get_content(pContent);
    RESULTSET* rset = resultset_create(shard_list_cb, &pContent);

    if (rset)
    {
        resultset_add_column(rset, "Database", MYSQL_DATABASE_MAXLEN, COL_TYPE_VARCHAR);
        resultset_add_column(rset, "Server", MYSQL_DATABASE_MAXLEN, COL_TYPE_VARCHAR);
        resultset_stream_mysql(rset, this->m_client);
        resultset_free(rset);
        rval = 0;
    }

    return rval;
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
    SERVER* target = this->m_shard.get_location(this->m_connect_db);

    if (target)
    {
        /* Send a COM_INIT_DB packet to the server with the right database
         * and set it as the client's active database */

        unsigned int qlen = this->m_connect_db.length();
        GWBUF* buffer = gwbuf_alloc(qlen + 5);

        if (buffer)
        {
            uint8_t *data = GWBUF_DATA(buffer);
            gw_mysql_set_byte3(data, qlen + 1);
            gwbuf_set_type(buffer, GWBUF_TYPE_MYSQL);
            data[3] = 0x0;
            data[4] = 0x2;
            memcpy(data + 5, this->m_connect_db.c_str(), qlen);
            DCB* dcb = NULL;

            if (get_shard_dcb(&dcb, target->unique_name))
            {
                dcb->func.write(dcb, buffer);
                MXS_DEBUG("USE '%s' sent to %s for session %p",
                          this->m_connect_db.c_str(),
                          target->unique_name,
                          this->m_client->session);
                rval = true;
            }
            else
            {
                MXS_INFO("Couldn't find target DCB for '%s'.", target->unique_name);
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
        MXS_INFO("Connecting to a non-existent database '%s'", this->m_connect_db.c_str());
        char errmsg[128 + MYSQL_DATABASE_MAXLEN + 1];
        sprintf(errmsg, "Unknown database '%s'", this->m_connect_db.c_str());
        if (this->m_config.debug)
        {
            sprintf(errmsg + strlen(errmsg), " ([%lu]: DB not found on connect)",
                    this->m_client->session->ses_id);
        }
        write_error_to_client(this->m_client,
                              SCHEMA_ERR_DBNOTFOUND,
                              SCHEMA_ERRSTR_DBNOTFOUND,
                              errmsg);
    }

    return rval;
}

void SchemaRouterSession::route_queued_query()
{
    GWBUF* tmp = this->m_queue;
    this->m_queue = this->m_queue->next;
    tmp->next = NULL;
#ifdef SS_DEBUG
    char* querystr = modutil_get_SQL(tmp);
    MXS_DEBUG("Sending queued buffer for session %p: %s",
              this->m_client->session,
              querystr);
    MXS_FREE(querystr);
#endif
    poll_add_epollin_event_to_dcb(this->m_client, tmp);
}

/**
 *
 * @param router_cli_ses Router client session
 * @return 1 if mapping is done, 0 if it is still ongoing and -1 on error
 */
int SchemaRouterSession::inspect_backend_mapping_states(backend_ref_t *bref,
                                                        GWBUF** wbuf)
{
    bool mapped = true;
    GWBUF* writebuf = *wbuf;
    backend_ref_t* bkrf = this->m_backends;

    for (int i = 0; i < this->m_backend_count; i++)
    {
        if (bref->dcb == bkrf[i].dcb && !BREF_IS_MAPPED(&bkrf[i]))
        {
            if (bref->map_queue)
            {
                writebuf = gwbuf_append(bref->map_queue, writebuf);
                bref->map_queue = NULL;
            }
            showdb_response_t rc = parse_showdb_response(&this->m_backends[i],
                                                         &writebuf);
            if (rc == SHOWDB_FULL_RESPONSE)
            {
                this->m_backends[i].mapped = true;
                MXS_DEBUG("Received SHOW DATABASES reply from %s for session %p",
                          this->m_backends[i].backend->server->unique_name,
                          this->m_client->session);
            }
            else if (rc == SHOWDB_PARTIAL_RESPONSE)
            {
                bref->map_queue = writebuf;
                writebuf = NULL;
                MXS_DEBUG("Received partial SHOW DATABASES reply from %s for session %p",
                          this->m_backends[i].backend->server->unique_name,
                          this->m_client->session);
            }
            else
            {
                DCB* client_dcb = NULL;

                if ((this->m_state & INIT_FAILED) == 0)
                {
                    if (rc == SHOWDB_DUPLICATE_DATABASES)
                    {
                        MXS_ERROR("Duplicate databases found, closing session.");
                    }
                    else
                    {
                        MXS_ERROR("Fatal error when processing SHOW DATABASES response, closing session.");
                    }
                    client_dcb = this->m_client;

                    /** This is the first response to the database mapping which
                     * has duplicate database conflict. Set the initialization bitmask
                     * to INIT_FAILED */
                    this->m_state |= INIT_FAILED;

                    /** Send the client an error about duplicate databases
                     * if there is a queued query from the client. */
                    if (this->m_queue)
                    {
                        GWBUF* error = modutil_create_mysql_err_msg(1, 0,
                                                                    SCHEMA_ERR_DUPLICATEDB,
                                                                    SCHEMA_ERRSTR_DUPLICATEDB,
                                                                    "Error: duplicate databases "
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

        if (BREF_IS_IN_USE(&bkrf[i]) && !BREF_IS_MAPPED(&bkrf[i]))
        {
            mapped = false;
            MXS_DEBUG("Still waiting for reply to SHOW DATABASES from %s for session %p",
                      bkrf[i].backend->server->unique_name,
                      this->m_client->session);
        }
    }
    *wbuf = writebuf;
    return mapped ? 1 : 0;
}

/**
 * Create a fake error message from a DCB.
 * @param fail_str Custom error message
 * @param dcb DCB to use as the origin of the error
 */
void create_error_reply(char* fail_str, DCB* dcb)
{
    MXS_INFO("change_current_db: failed to change database: %s", fail_str);
    GWBUF* errbuf = modutil_create_mysql_err_msg(1, 0, 1049, "42000", fail_str);

    if (errbuf == NULL)
    {
        MXS_ERROR("Creating buffer for error message failed.");
        return;
    }
    /** Set flags that help router to identify session commands reply */
    gwbuf_set_type(errbuf, GWBUF_TYPE_MYSQL);
    gwbuf_set_type(errbuf, GWBUF_TYPE_SESCMD_RESPONSE);
    gwbuf_set_type(errbuf, GWBUF_TYPE_RESPONSE_END);

    poll_add_epollin_event_to_dcb(dcb, errbuf);
}

/**
 * Read new database name from MYSQL_COM_INIT_DB packet or a literal USE ... COM_QUERY
 * packet, check that it exists in the hashtable and copy its name to MYSQL_session.
 *
 * @param dest Destination where the database name will be written
 * @param dbhash Hashtable containing valid databases
 * @param buf   Buffer containing the database change query
 *
 * @return true if new database is set, false if non-existent database was tried
 * to be set
 */
bool change_current_db(string& dest, Shard& shard, GWBUF* buf)
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
                MXS_INFO("change_current_db: database is on server: '%s'.", target->unique_name);
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
            size = *ptr + ((*(ptr + 2) << 8)) + (*(ptr + 3) << 16) +
                   (*(ptr + 4) << 24) + ((uintptr_t) * (ptr + 5) << 32) +
                   ((uintptr_t) * (ptr + 6) << 40) +
                   ((uintptr_t) * (ptr + 7) << 48) + ((uintptr_t) * (ptr + 8) << 56);
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
showdb_response_t SchemaRouterSession::parse_showdb_response(backend_ref_t* bref, GWBUF** buffer)
{
    unsigned char* ptr;
    SERVER* target = bref->backend->server;
    GWBUF* buf;
    bool duplicate_found = false;
    showdb_response_t rval = SHOWDB_PARTIAL_RESPONSE;

    if (buffer == NULL || *buffer == NULL)
    {
        return SHOWDB_FATAL_ERROR;
    }

    /** TODO: Don't make the buffer contiguous but process it as a buffer chain */
    *buffer = gwbuf_make_contiguous(*buffer);
    buf = modutil_get_complete_packets(buffer);

    if (buf == NULL)
    {
        return SHOWDB_PARTIAL_RESPONSE;
    }

    ptr = (unsigned char*) buf->start;

    if (PTR_IS_ERR(ptr))
    {
        MXS_INFO("SHOW DATABASES returned an error.");
        gwbuf_free(buf);
        return SHOWDB_FATAL_ERROR;
    }

    if (bref->n_mapping_eof == 0)
    {
        /** Skip column definitions */
        while (ptr < (unsigned char*) buf->end && !PTR_IS_EOF(ptr))
        {
            ptr += gw_mysql_get_byte3(ptr) + 4;
        }

        if (ptr >= (unsigned char*) buf->end)
        {
            MXS_INFO("Malformed packet for SHOW DATABASES.");
            *buffer = gwbuf_append(buf, *buffer);
            return SHOWDB_FATAL_ERROR;
        }

        atomic_add(&bref->n_mapping_eof, 1);
        /** Skip first EOF packet */
        ptr += gw_mysql_get_byte3(ptr) + 4;
    }

    while (ptr < (unsigned char*) buf->end && !PTR_IS_EOF(ptr))
    {
        int payloadlen = gw_mysql_get_byte3(ptr);
        int packetlen = payloadlen + 4;
        char* data = get_lenenc_str(ptr + 4);

        if (data)
        {
            if (this->m_shard.add_location(data, target))
            {
                MXS_INFO("<%s, %s>", target->unique_name, data);
            }
            else
            {
                if (!(this->m_router.m_ignored_dbs.find(data) != this->m_router.m_ignored_dbs.end() ||
                      (this->m_router.m_ignore_regex &&
                       pcre2_match(this->m_router.m_ignore_regex, (PCRE2_SPTR)data,
                                   PCRE2_ZERO_TERMINATED, 0, 0,
                                   this->m_router.m_ignore_match_data, NULL) >= 0)))
                {
                    duplicate_found = true;
                    SERVER *duplicate = this->m_shard.get_location(data);

                    MXS_ERROR("Database '%s' found on servers '%s' and '%s' for user %s@%s.",
                              data, target->unique_name, duplicate->unique_name,
                              this->m_client->user,
                              this->m_client->remote);
                }
            }
            MXS_FREE(data);
        }
        ptr += packetlen;
    }

    if (ptr < (unsigned char*) buf->end && PTR_IS_EOF(ptr) && bref->n_mapping_eof == 1)
    {
        atomic_add(&bref->n_mapping_eof, 1);
        MXS_INFO("SHOW DATABASES fully received from %s.",
                 bref->backend->server->unique_name);
    }
    else
    {
        MXS_INFO("SHOW DATABASES partially received from %s.",
                 bref->backend->server->unique_name);
    }

    gwbuf_free(buf);

    if (duplicate_found)
    {
        rval = SHOWDB_DUPLICATE_DATABASES;
    }
    else if (bref->n_mapping_eof == 2)
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
int SchemaRouterSession::gen_databaselist()
{
    DCB* dcb;
    const char* query = "SHOW DATABASES";
    GWBUF *buffer, *clone;
    int i, rval = 0;
    unsigned int len;

    for (i = 0; i < this->m_backend_count; i++)
    {
        this->m_backends[i].mapped = false;
        this->m_backends[i].n_mapping_eof = 0;
    }

    this->m_state |= INIT_MAPPING;
    this->m_state &= ~INIT_UNINT;
    len = strlen(query) + 1;
    buffer = gwbuf_alloc(len + 4);
    uint8_t *data = GWBUF_DATA(buffer);
    *(data) = len;
    *(data + 1) = len >> 8;
    *(data + 2) = len >> 16;
    *(data + 3) = 0x0;
    *(data + 4) = 0x03;
    memcpy(data + 5, query, strlen(query));

    for (i = 0; i < this->m_backend_count; i++)
    {
        if (BREF_IS_IN_USE(&this->m_backends[i]) &&
            !BREF_IS_CLOSED(&this->m_backends[i]) &
            SERVER_IS_RUNNING(this->m_backends[i].backend->server))
        {
            clone = gwbuf_clone(buffer);
            dcb = this->m_backends[i].dcb;
            rval |= !dcb->func.write(dcb, clone);
            MXS_DEBUG("Wrote SHOW DATABASES to %s for session %p: returned %d",
                      this->m_backends[i].backend->server->unique_name,
                      this->m_client->session,
                      rval);
        }
    }
    gwbuf_free(buffer);
    return !rval;
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
    SERVER *rval = NULL;
    bool has_dbs = false; /**If the query targets any database other than the current one*/
    const QC_FIELD_INFO* info;
    size_t n_info;

    qc_get_field_info(buffer, &info, &n_info);

    for (size_t i = 0; i < n_info; i++)
    {
        if (info[i].database)
        {
            if (strcmp(info[i].database, "information_schema") == 0 && rval == NULL)
            {
                has_dbs = false;
            }
            else
            {
                SERVER* target = this->m_shard.get_location(info[i].database);

                if (target)
                {
                    if (rval && target != rval)
                    {
                        MXS_ERROR("Query targets databases on servers '%s' and '%s'. "
                                  "Cross database queries across servers are not supported.",
                                  rval->unique_name, target->unique_name);
                    }
                    else if (rval == NULL)
                    {
                        rval = target;
                        has_dbs = true;
                        MXS_INFO("Query targets database '%s' on server '%s'",
                                 info[i].database, rval->unique_name);
                    }
                }
            }
        }
    }

    /* Check if the query is a show tables query with a specific database */

    if (qc_query_is_type(qtype, QUERY_TYPE_SHOW_TABLES))
    {
        char *query = modutil_get_SQL(buffer);
        char *tmp;

        if ((tmp = strcasestr(query, "from")))
        {
            const char *delim = "` \n\t;";
            char *saved, *tok = strtok_r(tmp, delim, &saved);
            tok = strtok_r(NULL, delim, &saved);

            if (tok)
            {
                rval = this->m_shard.get_location(tok);

                if (rval)
                {
                    MXS_INFO("SHOW TABLES with specific database '%s' on server '%s'", tok, tmp);
                }
            }
        }
        MXS_FREE(query);

        if (rval == NULL)
        {
            rval = this->m_shard.get_location(this->m_current_db);

            if (rval)
            {
                MXS_INFO("SHOW TABLES query, current database '%s' on server '%s'",
                         this->m_current_db.c_str(), rval->unique_name);
            }
        }
        else
        {
            has_dbs = true;
        }
    }
    else if (buffer->hint && buffer->hint->type == HINT_ROUTE_TO_NAMED_SERVER)
    {
        for (int i = 0; i < this->m_backend_count; i++)
        {
            char *srvnm = this->m_backends[i].backend->server->unique_name;

            if (strcmp(srvnm, (char*)buffer->hint->data) == 0)
            {
                rval = this->m_backends[i].backend->server;
                MXS_INFO("Routing hint found (%s)", rval->unique_name);
            }
        }

        if (rval == NULL && !has_dbs && this->m_current_db.length())
        {
            /**
             * If the target name has not been found and the session has an
             * active database, set is as the target
             */

            rval = this->m_shard.get_location(this->m_current_db);

            if (rval)
            {
                MXS_INFO("Using active database '%s' on '%s'",
                         this->m_current_db.c_str(), rval->unique_name);
            }
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
    backend_ref_t* backend_ref;
    int i;
    bool succp = false;


    ss_dassert(p_dcb != NULL && *(p_dcb) == NULL);

    if (p_dcb == NULL || name == NULL)
    {
        goto return_succp;
    }
    backend_ref = this->m_backends;

    for (i = 0; i < this->m_backend_count; i++)
    {
        SERVER_REF* b = backend_ref[i].backend;
        /**
         * To become chosen:
         * backend must be in use, name must match, and
         * the backend state must be RUNNING
         */
        if (BREF_IS_IN_USE((&backend_ref[i])) &&
            (strncasecmp(name, b->server->unique_name, PATH_MAX) == 0) &&
            SERVER_IS_RUNNING(b->server))
        {
            *p_dcb = backend_ref[i].dcb;
            succp = true;
            ss_dassert(backend_ref[i].dcb->state != DCB_STATE_ZOMBIE);
            goto return_succp;
        }
    }

return_succp:
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
route_target_t get_shard_route_target(uint32_t qtype)
{
    route_target_t target = TARGET_UNDEFINED;

    /**
     * These queries are not affected by hints
     */
    if (qc_query_is_type(qtype, QUERY_TYPE_SESSION_WRITE) ||
        qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_WRITE) ||
        qc_query_is_type(qtype, QUERY_TYPE_USERVAR_WRITE) ||
        qc_query_is_type(qtype, QUERY_TYPE_PREPARE_STMT) ||
        qc_query_is_type(qtype, QUERY_TYPE_PREPARE_NAMED_STMT) ||
        qc_query_is_type(qtype, QUERY_TYPE_ENABLE_AUTOCOMMIT) ||
        qc_query_is_type(qtype, QUERY_TYPE_DISABLE_AUTOCOMMIT))
    {
        /** hints don't affect on routing */
        target = TARGET_ALL;
    }
    else if (qc_query_is_type(qtype, QUERY_TYPE_SYSVAR_READ) ||
             qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_READ))
    {
        target = TARGET_ANY;
    }

    return target;
}

/**
 * Callback for the database list streaming.
 * @param rset Result set which is being processed
 * @param data Pointer to struct string_array containing the database names
 * @return New resultset row or NULL if no more data is available. If memory allocation
 * failed, NULL is returned.
 */
RESULT_ROW *result_set_cb(struct resultset * rset, void *data)
{
    RESULT_ROW *row = resultset_make_row(rset);
    ServerMap* arr = (ServerMap*) data;

    if (row)
    {
        if (arr->size() > 0 && resultset_row_set(row, 0, arr->begin()->first.c_str()))
        {
            arr->erase(arr->begin());
        }
        else
        {
            resultset_free_row(row);
            row = NULL;
        }
    }

    return row;
}

/**
 * Generates a custom SHOW DATABASES result set from all the databases in the
 * hashtable. Only backend servers that are up and in a proper state are listed
 * in it.
 * @param router Router instance
 * @param client Router client session
 * @return True if the sending of the database list was successful, otherwise false
 */
bool SchemaRouterSession::send_database_list()
{
    bool rval = false;

    ServerMap dblist;
    this->m_shard.get_content(dblist);

    RESULTSET* resultset = resultset_create(result_set_cb, &dblist);

    if (resultset_add_column(resultset, "Database", MYSQL_DATABASE_MAXLEN,
                             COL_TYPE_VARCHAR))
    {
        resultset_stream_mysql(resultset, this->m_client);
        rval = true;
    }
    resultset_free(resultset);

    return rval;
}

void bref_clear_state(backend_ref_t* bref, bref_state_t state)
{
    if (bref == NULL)
    {
        MXS_ERROR("[%s] Error: NULL parameter.", __FUNCTION__);
        return;
    }
    if (state != BREF_WAITING_RESULT)
    {
        bref->state &= ~state;
    }
    else
    {
        /** Decrease global operation count */
        int prev2 = atomic_add(&bref->backend->server->stats.n_current_ops, -1);
        ss_dassert(prev2 > 0);

        if (prev2 <= 0)
        {
            MXS_ERROR("[%s] Error: negative current operation count in backend %s:%u",
                      __FUNCTION__,
                      bref->backend->server->name,
                      bref->backend->server->port);
        }
    }
}

void bref_set_state(backend_ref_t* bref, bref_state_t state)
{
    if (bref == NULL)
    {
        MXS_ERROR("[%s] Error: NULL parameter.", __FUNCTION__);
        return;
    }
    if (state != BREF_WAITING_RESULT)
    {
        bref->state |= state;
    }
    else
    {
        /** Increase global operation count */
        int prev2 = atomic_add(&bref->backend->server->stats.n_current_ops, 1);
        ss_dassert(prev2 >= 0);

        if (prev2 < 0)
        {
            MXS_ERROR("[%s] Error: negative current operation count in backend %s:%u",
                      __FUNCTION__,
                      bref->backend->server->name,
                      bref->backend->server->port);
        }
    }
}

/**
 * @node Search all RUNNING backend servers and connect
 *
 * Parameters:
 * @param backend_ref - in, use, out
 *      Pointer to backend server reference object array.
 *      NULL is not allowed.
 *
 * @param router_nservers - in, use
 *      Number of backend server pointers pointed to by b.
 *
 * @param session - in, use
 *      MaxScale session pointer used when connection to backend is established.
 *
 * @param  router - in, use
 *      Pointer to router instance. Used when server states are qualified.
 *
 * @return true, if at least one master and one slave was found.
 *
 *
 * @details It is assumed that there is only one available server.
 *      There will be exactly as many backend references than there are
 *      connections because all servers are supposed to be operational. It is,
 *      however, possible that there are less available servers than expected.
 */
bool connect_backend_servers(backend_ref_t*   backend_ref,
                             int              router_nservers,
                             MXS_SESSION*     session)
{
    bool succp = false;
    int servers_found = 0;
    int servers_connected = 0;
    int slaves_connected = 0;

    if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
    {
        MXS_INFO("Servers and connection counts:");

        for (int i = 0; i < router_nservers; i++)
        {
            SERVER_REF* b = backend_ref[i].backend;

            MXS_INFO("MaxScale connections : %d (%d) in \t%s:%d %s",
                     b->connections,
                     b->server->stats.n_current,
                     b->server->name,
                     b->server->port,
                     STRSRVSTATUS(b->server));
        }
    }
    /**
     * Scan server list and connect each of them. None should fail or session
     * can't be established.
     */
    for (int i = 0; i < router_nservers; i++)
    {
        SERVER_REF* b = backend_ref[i].backend;

        if (SERVER_IS_RUNNING(b->server))
        {
            servers_found += 1;

            /** Server is already connected */
            if (BREF_IS_IN_USE((&backend_ref[i])))
            {
                slaves_connected += 1;
            }
            /** New server connection */
            else
            {
                backend_ref[i].dcb = dcb_connect(b->server,
                                                 session,
                                                 b->server->protocol);

                if (backend_ref[i].dcb != NULL)
                {
                    servers_connected += 1;
                    /**
                     * When server fails, this callback
                     * is called.
                     * !!! Todo, routine which removes
                     * corresponding entries from the hash
                     * table.
                     */

                    backend_ref[i].state = 0;
                    bref_set_state(&backend_ref[i], BREF_IN_USE);
                    /**
                     * Increase backend connection counter.
                     * Server's stats are _increased_ in
                     * dcb.c:dcb_alloc !
                     * But decreased in the calling function
                     * of dcb_close.
                     */
                    atomic_add(&b->connections, 1);
                }
                else
                {
                    succp = false;
                    MXS_ERROR("Unable to establish "
                              "connection with slave %s:%d",
                              b->server->name,
                              b->server->port);
                    /* handle connect error */
                    break;
                }
            }
        }
    }

    if (servers_connected > 0)
    {
        succp = true;

        if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            for (int i = 0; i < router_nservers; i++)
            {
                SERVER_REF* b = backend_ref[i].backend;

                if (BREF_IS_IN_USE((&backend_ref[i])))
                {
                    MXS_INFO("Connected %s in \t%s:%d",
                             STRSRVSTATUS(b->server),
                             b->server->name,
                             b->server->port);
                }
            }
        }
    }

    return succp;
}
