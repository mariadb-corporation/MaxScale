/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
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
#include <maxbase/alloc.hh>
#include <maxscale/protocol/mariadb/resultset.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

#include <mysqld_error.h>

using mxs::Parser;

namespace schemarouter
{

bool connect_backend_servers(SRBackendList& backends, MXS_SESSION* session);

enum route_target get_shard_route_target(uint32_t qtype);
bool              detect_show_shards(const Parser& parser, const GWBUF& query);

SchemaRouterSession::SchemaRouterSession(MXS_SESSION* session,
                                         SchemaRouter* router,
                                         SRBackendList backends)
    : mxs::RouterSession(session)
    , m_client(static_cast<MariaDBClientConnection*>(session->client_connection()))
    , m_backends(std::move(backends))
    , m_config(*router->m_config.values())
    , m_router(router)
    , m_key(get_cache_key())
    , m_shard(m_router->m_shard_manager.get_shard(m_key, m_config.refresh_interval.count()))
    , m_state(0)
    , m_load_target(NULL)
{
    m_mysql_session = static_cast<MYSQL_session*>(session->protocol_data());
    auto current_db = m_mysql_session->auth_data->default_db;

    // TODO: The following is not pretty and is bound to cause problems in the future.

    /* To enable connecting directly to a sharded database we first need
     * to disable it for the client DCB's protocol so that we can connect to them */
    if (m_mysql_session->client_capabilities() & GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB && !current_db.empty())
    {
        m_mysql_session->client_caps.basic_capabilities &= ~GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB;
        m_mysql_session->auth_data->default_db.clear();

        /* Store the database the client is connecting to */
        m_connect_db = current_db;
        m_state |= INIT_USE_DB;

        MXB_INFO("Client logging in directly to a database '%s', "
                 "postponing until databases have been mapped.", current_db.c_str());
    }

    mxb::atomic::add(&m_router->m_stats.sessions, 1);
}

SchemaRouterSession::~SchemaRouterSession()
{
    if (m_dcid)
    {
        m_pSession->cancel_dcall(m_dcid);
    }

    if (m_state & INIT_MAPPING)
    {
        m_router->m_shard_manager.cancel_update(m_key);
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

static void inspect_query(const Parser& parser,
                          const GWBUF& packet,
                          uint32_t* type,
                          mxs::sql::OpCode* op,
                          uint8_t command)
{
    GWBUF* bufptr = const_cast<GWBUF*>(&packet);
    switch (command)
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
        *type = mxs::sql::TYPE_SESSION_WRITE;
        break;

    case MXS_COM_CREATE_DB: /**< 5 DDL must go to the master */
    case MXS_COM_DROP_DB:   /**< 6 DDL must go to the master */
        *type = mxs::sql::TYPE_WRITE;
        break;

    case MXS_COM_QUERY:
        *type = parser.get_type_mask(*bufptr);
        *op = parser.get_operation(*bufptr);
        break;

    case MXS_COM_STMT_PREPARE:
        *type = parser.get_type_mask(*bufptr);
        *type |= mxs::sql::TYPE_PREPARE_STMT;
        break;

    case MXS_COM_STMT_EXECUTE:
        /** Parsing is not needed for this type of packet */
        *type = mxs::sql::TYPE_EXEC_STMT;
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

    if (mxb_log_should_log(LOG_INFO))
    {
        MXB_INFO("> Command: %s, stmt: %s %s%s",
                 mariadb::cmd_to_string(command),
                 std::string(parser.get_sql(packet)).c_str(),
                 (packet.hints.empty() ? "" : ", Hint:"),
                 (packet.hints.empty() ? "" : Hint::type_to_str(packet.hints[0].type)));
    }
}

mxs::Target* SchemaRouterSession::resolve_query_target(const GWBUF& packet, uint32_t type,
                                                       uint8_t command, enum route_target& route_target)
{
    mxs::Target* target = NULL;

    if (route_target != TARGET_NAMED_SERVER)
    {
        /** We either don't know or don't care where this query should go */
        target = get_shard_target(packet, type);

        if (target && target->is_usable())
        {
            route_target = TARGET_NAMED_SERVER;
        }
    }

    if (TARGET_IS_UNDEFINED(route_target))
    {
        /** We don't know where to send this. Route it to either the server with
         * the current default database or to the first available server. */
        target = get_shard_target(packet, type);

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
        if (SRBackend* b = get_any_backend())
        {
            route_target = TARGET_NAMED_SERVER;
            target = b->target();
        }
        else
        {
            /**No valid backends alive*/
            MXB_ERROR("Failed to route query, no backends are available.");
        }
    }

    return target;
}

static bool is_empty_packet(const GWBUF& packet)
{
    bool rval = false;
    if (packet.length() == MYSQL_HEADER_LEN && mariadb::get_header(packet.data()).pl_length == 0)
    {
        rval = true;
    }
    return rval;
}

mxs::Target* SchemaRouterSession::get_valid_target(const std::set<mxs::Target*>& candidates)
{
    for (const auto& b : m_backends)
    {
        if (b->in_use() && candidates.count(b->target()))
        {
            return b->target();
        }
    }

    return nullptr;
}

bool SchemaRouterSession::routeQuery(GWBUF&& packet)
{
    if (m_shard.empty() && (m_state & INIT_MAPPING) == 0)
    {
        if (m_dcid)
        {
            // The delayed call is already in place, let it take care of the shard update
            m_queue.push_back(std::move(packet));
            return 1;
        }

        // Check if another session has managed to update the shard cache
        m_shard = m_router->m_shard_manager.get_shard(m_key, m_config.refresh_interval.count());

        if (m_shard.empty())
        {
            // No entries in the cache, try to start an update
            if (m_router->m_shard_manager.start_update(m_key))
            {
                // No other sessions are doing an update for this user, start one
                query_databases();
            }
            else
            {
                // Wait for the other session to finish its update and reuse that result
                mxb_assert(m_dcid == 0);
                m_queue.push_back(std::move(packet));

                auto worker = mxs::RoutingWorker::get_current();
                m_dcid = m_pSession->dcall(1000ms, &SchemaRouterSession::delay_routing, this);
                MXB_INFO("Waiting for the database mapping to be completed by another session");

                return 1;
            }
        }
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
        m_queue.push_back(std::move(packet));
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

    uint8_t command = mxs_mysql_get_command(packet);
    mxs::Target* target = NULL;
    uint32_t type = mxs::sql::TYPE_UNKNOWN;
    mxs::sql::OpCode op = mxs::sql::OP_UNDEFINED;
    enum route_target route_target = TARGET_UNDEFINED;

    if (m_load_target)
    {
        /** A load data local infile is active */
        target = m_load_target;
        route_target = TARGET_NAMED_SERVER;

        if (is_empty_packet(packet))
        {
            m_load_target = NULL;
        }
    }
    else
    {
        inspect_query(parser(), packet, &type, &op, command);

        /** Create the response to the SHOW DATABASES from the mapped databases */
        if (Parser::type_mask_contains(type, mxs::sql::TYPE_SHOW_DATABASES))
        {
            send_databases();
            return 1;
        }
        else if (detect_show_shards(parser(), packet))
        {
            if (send_shards())
            {
                ret = 1;
            }
            return ret;
        }

        route_target = get_shard_route_target(type);

        // Route all transaction control commands to all backends. This will keep the transaction state
        // consistent even if no default database is used or if the default database being used is located
        // on more than one node.
        if (packet.hints.empty()
            && (type & (mxs::sql::TYPE_BEGIN_TRX | mxs::sql::TYPE_COMMIT | mxs::sql::TYPE_ROLLBACK)))
        {
            MXB_INFO("Routing trx control statement to all nodes.");
            route_target = TARGET_ALL;
        }

        /**
         * Find a suitable server that matches the requirements of @c route_target
         */
        if (command == MXS_COM_INIT_DB || op == mxs::sql::OP_CHANGE_DB)
        {
            /** The default database changes must be routed to a specific server */
            if (change_current_db(packet, command))
            {
                return 1;
            }
            else
            {
                if (m_config.refresh_databases && m_shard.stale(m_config.refresh_interval.count()))
                {
                    m_queue.push_back(std::move(packet));
                    query_databases();
                    return 1;
                }

                // If we don't end up refreshing the databases, we'll just route it as a normal query to a
                // random backend.
                route_target = TARGET_ANY;
                MXB_INFO("Client is trying to use an unknown database.");
            }
        }

        if (TARGET_IS_ALL(route_target))
        {
            /** Session commands, route to all servers */
            if (route_session_write(std::move(packet), command))
            {
                mxb::atomic::add(&m_router->m_stats.n_sescmd, 1, mxb::atomic::RELAXED);
                mxb::atomic::add(&m_router->m_stats.n_queries, 1, mxb::atomic::RELAXED);
                ret = 1;
            }
        }
        else if (target == NULL)
        {
            target = resolve_query_target(packet, type, command, route_target);
        }
    }

    if (TARGET_IS_NAMED_SERVER(route_target) && target)
    {
        uint8_t cmd = mxs_mysql_get_command(packet);

        if (SRBackend* bref = get_shard_backend(target->name()))
        {
            if (op == mxs::sql::OP_LOAD_LOCAL)
            {
                m_load_target = bref->target();
            }

            // Store the target we're routing the query to. This can be used later if a situation is
            // encountered where a query has no "natural" target (e.g. CREATE DATABASE with no default
            // database).
            m_prev_target = bref;

            MXB_INFO("Route query to \t%s <", bref->name());

            auto responds = protocol_data()->will_respond(packet) ?
                mxs::Backend::EXPECT_RESPONSE :
                mxs::Backend::NO_RESPONSE;

            if (bref->write(std::move(packet), responds))
            {
                /** Add one query response waiter to backend reference */
                mxb::atomic::add(&m_router->m_stats.n_queries, 1, mxb::atomic::RELAXED);
                ret = 1;
            }
        }
        else
        {
            MXB_ERROR("Could not find valid server for %s, closing connection.", mariadb::cmd_to_string(cmd));
        }
    }

    return ret;
}
void SchemaRouterSession::handle_mapping_reply(SRBackend* bref, const mxs::Reply& reply)
{
    int rc = inspect_mapping_states(bref, reply);

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
            MXB_INFO("Routing stored query");
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

bool SchemaRouterSession::clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    SRBackend* bref = static_cast<SRBackend*>(down.back()->get_userdata());

    const auto& error = reply.error();

    if (error.is_unexpected_error())
    {
        // All unexpected errors are related to server shutdown.
        bref->set_close_reason(std::string("Server '") + bref->name() + "' is shutting down");

        // The server sent an error that we either didn't expect or we don't want. If retrying is going to
        // take place, it'll be done in handleError.
        if (!bref->is_waiting_result() || !reply.has_started())
        {
            // The buffer contains either an ERR packet, in which case the resultset hasn't started yet, or a
            // resultset with a trailing ERR packet. The full resultset can be discarded as the client hasn't
            // received it yet. In theory we could return this to the client but we don't know if it was
            // interrupted or not so the safer option is to retry it.
            return false;
        }
    }

    if (bref->should_ignore_response())
    {
        packet.clear();
    }

    if (reply.is_complete())
    {
        MXB_INFO("Reply complete from '%s'", bref->name());
        bref->ack_write();
    }

    if (m_state & INIT_MAPPING)
    {
        handle_mapping_reply(bref, reply);
        packet.clear();
    }
    else if (m_state & INIT_USE_DB)
    {
        MXB_INFO("Reply to USE '%s' received for session %p", m_connect_db.c_str(), m_pSession);
        packet.clear();
        handle_default_db_response();
    }
    else if (m_queue.size())
    {
        mxb_assert(m_state == INIT_READY);
        route_queued_query();
    }

    int32_t rc = 1;

    if (packet)
    {
        rc = RouterSession::clientReply(std::move(packet), down, reply);
    }

    return rc;
}

bool SchemaRouterSession::handleError(mxs::ErrorType type,
                                      const std::string& message,
                                      mxs::Endpoint* pProblem,
                                      const mxs::Reply& reply)
{
    SRBackend* bref = static_cast<SRBackend*>(pProblem->get_userdata());
    mxb_assert(bref);

    if (bref->is_waiting_result())
    {
        if ((m_state & (INIT_USE_DB | INIT_MAPPING)) == INIT_USE_DB)
        {
            handle_default_db_response();
        }

        if (m_state & INIT_MAPPING)
        {
            // An error during the shard mapping is not a fatal response. Broken servers are allowed to exist
            // as the user might not exist on all backends.
            mxs::Reply tmp;
            uint16_t errcode = 1927;    // ER_CONNECTION_KILLED
            std::string sqlstate = "HY000";
            tmp.set_error(errcode, sqlstate.begin(), sqlstate.end(), message.begin(), message.end());
            handle_mapping_reply(bref, tmp);
        }
        else if (!bref->should_ignore_response())
        {
            // A result that was to be returned to the client was expected from this backend. Since the
            // schemarouter does not have any retrying capabilities, the only option is to kill the session.
            m_pSession->kill();
        }
    }

    bref->close(type == mxs::ErrorType::PERMANENT ? Backend::CLOSE_FATAL : Backend::CLOSE_NORMAL);

    return have_servers() || mxs::RouterSession::handleError(
        type, "All connections have failed: " + message, pProblem, reply);
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
    m_router->m_shard_manager.update_shard(m_shard, m_key);
}

/**
 * Extract the database name from a COM_INIT_DB or literal USE ... query.
 * @param buf Buffer with the database change query
 * @param str Pointer where the database name is copied
 * @return True for success, false for failure
 */
std::pair<bool, std::string> extract_database(const Parser& parser, const GWBUF& buf)
{
    bool ok = true;
    std::string rval;
    uint8_t command = mxs_mysql_get_command(buf);

    if (command == MXS_COM_QUERY && parser.get_operation(const_cast<GWBUF&>(buf)) == mxs::sql::OP_CHANGE_DB)
    {
        auto tokens = mxb::strtok(parser.get_sql(buf), "` \n\t;");

        if (tokens.size() < 2 || strcasecmp(tokens[0].c_str(), "use") != 0)
        {
            MXB_INFO("extract_database: Malformed change database packet.");
            ok = false;
        }
        else
        {
            rval = tokens[1];
        }
    }
    else if (command == MXS_COM_INIT_DB)
    {
        rval.assign(buf.begin() + 5, buf.end());
    }

    return {ok, std::move(rval)};
}

bool SchemaRouterSession::write_session_command(SRBackend* backend, GWBUF&& buffer, uint8_t cmd)
{
    bool ok = true;
    mxs::Backend::response_type type = mxs::Backend::NO_RESPONSE;

    if (protocol_data()->will_respond(buffer))
    {
        if (backend == m_sescmd_replier)
        {
            MXB_INFO("Will return response from '%s' to the client", backend->name());
            type = mxs::Backend::EXPECT_RESPONSE;
        }
        else
        {
            type = mxs::Backend::IGNORE_RESPONSE;
        }
    }

    if (backend->write(std::move(buffer), type))
    {
        MXB_INFO("Route query to %s: %s", backend->is_master() ? "primary" : "replica", backend->name());
    }
    else
    {
        MXB_ERROR("Failed to execute session command in %s", backend->name());
        backend->close();
        ok = false;
    }

    return ok;
}

SRBackend* SchemaRouterSession::get_any_backend()
{
    if (m_prev_target && m_prev_target->in_use())
    {
        MXB_INFO("Using previous target: %s", m_prev_target->name());
        return m_prev_target;
    }

    for (const auto& b : m_backends)
    {
        if (b->in_use() && m_shard.uses_target(b->target()))
        {
            return b.get();
        }
    }

    for (const auto& b : m_backends)
    {
        if (b->in_use())
        {
            return b.get();
        }
    }

    return nullptr;
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
bool SchemaRouterSession::route_session_write(GWBUF&& querybuf, uint8_t command)
{
    bool ok = false;
    mxb::atomic::add(&m_stats.longest_sescmd, 1, mxb::atomic::RELAXED);
    m_sescmd_replier = get_any_backend();

    for (const auto& b : m_backends)
    {
        if (b->in_use() && write_session_command(b.get(), querybuf.shallow_clone(), command))
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
bool detect_show_shards(const Parser& parser, const GWBUF& query)
{
    bool rval = false;

    if (!mariadb::is_com_query_or_prepare(query))
    {
        return false;
    }

    auto tokens = mxb::strtok(parser.get_sql(query), " ");

    if (tokens.size() >= 2
        && strcasecmp(tokens[0].c_str(), "show") == 0
        && strcasecmp(tokens[1].c_str(), "shards") == 0)
    {
        rval = true;
    }

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

    for (const auto& db : m_shard.get_content())
    {
        for (const auto& tbl : db.second)
        {
            for (const auto* t : tbl.second)
            {
                set->add_row({db.first + "." + tbl.first, t->name()});
            }
        }
    }

    set_response(set->as_buffer());

    return true;
}

void SchemaRouterSession::write_error_to_client(int errnum, const char* mysqlstate, const char* errmsg)
{
    set_response(mariadb::create_error_packet(1, errnum, mysqlstate, errmsg));
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
        GWBUF buffer(qlen + 5);
        uint8_t* data = buffer.data();

        mariadb::set_byte3(data, qlen + 1);
        data[3] = 0x0;
        data[4] = MXS_COM_INIT_DB;
        memcpy(data + 5, m_connect_db.c_str(), qlen);

        if (auto backend = get_shard_backend(target->name()))
        {
            backend->write(std::move(buffer));
            ++m_num_init_db;
            rval = true;
        }
    }

    if (!rval)
    {
        /** Unknown database, hang up on the client*/
        MXB_INFO("Connecting to a nonexistent database '%s'", m_connect_db.c_str());
        char errmsg[128 + MYSQL_DATABASE_MAXLEN + 1];
        sprintf(errmsg, "Unknown database '%s'", m_connect_db.c_str());
        if (m_config.debug)
        {
            sprintf(errmsg + strlen(errmsg), " ([%" PRIu64 "]: DB not found on connect)", m_pSession->id());
        }
        write_error_to_client(SCHEMA_ERR_DBNOTFOUND, SCHEMA_ERRSTR_DBNOTFOUND, errmsg);
    }

    return rval;
}

void SchemaRouterSession::route_queued_query()
{
    GWBUF tmp = std::move(m_queue.front());
    m_queue.pop_front();

    MXB_INFO("Routing queued query: %s", get_sql_string(tmp).c_str());

    m_pSession->delay_routing(this, std::move(tmp), 0);
}

bool SchemaRouterSession::delay_routing()
{
    MXS_SESSION::Scope scope(m_pSession);
    bool rv = false;

    mxb_assert(m_shard.empty());
    m_shard = m_router->m_shard_manager.get_shard(m_key, m_config.refresh_interval.count());

    if (!m_shard.empty())
    {
        MXB_INFO("Another session updated the shard information, reusing the result");
        route_queued_query();
        m_dcid = 0;
    }
    else if (m_router->m_shard_manager.start_update(m_key))
    {
        // No other sessions are doing an update, start our own update
        query_databases();
        m_dcid = 0;
    }
    else
    {
        // We're still waiting for an update from another session
        rv = true;
    }

    return rv;
}

bool SchemaRouterSession::have_duplicates() const
{
    bool duplicates = false;

    for (const auto& db : m_shard.get_content())
    {
        for (const auto& tbl : db.second)
        {
            if (tbl.second.size() > 1)
            {
                auto name = db.first + "." + tbl.first;

                if (!ignore_duplicate_table(name))
                {
                    std::vector<const char*> data;

                    for (const auto* t : tbl.second)
                    {
                        data.push_back(t->name());
                    }

                    duplicates = true;
                    MXB_ERROR("'%s' found on servers %s for user %s.",
                              name.c_str(), mxb::join(data, ",", "'").c_str(),
                              m_pSession->user_and_host().c_str());
                }
            }
        }
    }

    return duplicates;
}

/**
 *
 * @param router_cli_ses Router client session
 * @return 1 if mapping is done, 0 if it is still ongoing and -1 on error
 */
int SchemaRouterSession::inspect_mapping_states(SRBackend* b, const mxs::Reply& reply)
{
    bool mapped = true;

    if (!b->is_mapped())
    {
        enum showdb_response rc = parse_mapping_response(b, reply);

        if (rc == SHOWDB_FULL_RESPONSE && have_duplicates())
        {
            rc = SHOWDB_DUPLICATE_DATABASES;
        }

        if (rc == SHOWDB_FULL_RESPONSE)
        {
            b->set_mapped(true);
            MXB_DEBUG("Received SHOW DATABASES reply from '%s' (%lu rows)", b->name(), reply.rows_read());
        }
        else if (rc == SHOWDB_FATAL_ERROR)
        {
            auto err = mariadb::create_error_packet(
                1, SCHEMA_ERR_DUPLICATEDB, SCHEMA_ERRSTR_DUPLICATEDB,
                ("Error: database mapping failed due to: " + reply.error().message()).c_str());

            mxs::ReplyRoute route;
            RouterSession::clientReply(std::move(err), route, reply);
            return -1;
        }
        else if (rc == SHOWDB_PARTIAL_RESPONSE)
        {
            MXB_INFO("Partial response from '%s'(%lu rows)", b->name(), reply.rows_read());
        }
        else
        {
            if ((m_state & INIT_FAILED) == 0)
            {
                if (rc == SHOWDB_DUPLICATE_DATABASES)
                {
                    MXB_ERROR("Duplicate tables found, closing session.");
                }
                else
                {
                    MXB_ERROR("Fatal error when processing SHOW DATABASES response, closing session.");
                }

                /** This is the first response to the database mapping which
                 * has duplicate database conflict. Set the initialization bitmask
                 * to INIT_FAILED */
                m_state |= INIT_FAILED;

                /** Send the client an error about duplicate databases
                 * if there is a queued query from the client. */
                if (!m_queue.empty())
                {
                    auto err = mariadb::create_error_packet(
                        1, SCHEMA_ERR_DUPLICATEDB, SCHEMA_ERRSTR_DUPLICATEDB,
                        "Error: duplicate tables found on two different shards.");

                    mxs::ReplyRoute route;
                    RouterSession::clientReply(std::move(err), route, mxs::Reply());
                }
            }

            return -1;
        }
    }

    return std::all_of(
        m_backends.begin(), m_backends.end(), [](const auto& b) {
        return !b->in_use() || b->is_mapped();
    });
}

/**
 * Read new database name from COM_INIT_DB packet or a literal USE ... COM_QUERY
 * packet and change the default database on the relevant backends.
 *
 * @param buf Buffer containing the database change query
 * @param cmd The command being executed
 *
 * @return True if new database was set and a query was executed, false if nonexistent database was tried
 *         to be used and it wasn't found on any of the backends.
 */
bool SchemaRouterSession::change_current_db(const GWBUF& buf, uint8_t cmd)
{
    bool succp = false;
    auto [ok, db] = extract_database(parser(), buf);

    if (ok)
    {
        auto targets = m_shard.get_all_locations(db);
        m_sescmd_replier = nullptr;

        for (const auto& b : m_backends)
        {
            if (b->in_use() && targets.count(b->target()))
            {
                // This must be set before the call to write_session_command is made as the replier is
                // used in it to determine whether the response should be discarded or not.
                if (!m_sescmd_replier)
                {
                    m_sescmd_replier = b.get();
                }

                if (write_session_command(b.get(), buf.shallow_clone(), cmd))
                {
                    succp = true;
                }
            }
        }

        if (succp)
        {
            m_current_db = db;
        }
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
std::string get_lenenc_str(uint8_t** input)
{
    std::string rv;
    uint8_t* ptr = *input;

    if (*ptr < 251)
    {
        rv = std::string((char*)ptr + 1, *ptr);
        ptr += 1;
    }
    else
    {
        switch (*(ptr))
        {
        case 0xfc:
            rv = std::string((char*)ptr + 2, mariadb::get_byte2(ptr));
            ptr += 2;
            break;

        case 0xfd:
            rv = std::string((char*)ptr + 3, mariadb::get_byte3(ptr));
            ptr += 3;
            break;

        case 0xfe:
            rv = std::string((char*)ptr + 8, mariadb::get_byte8(ptr));
            ptr += 8;
            break;

        default:
            mxb_assert(!true);
            break;
        }
    }

    ptr += rv.size();
    *input = ptr;

    return rv;
}

// We could also use a transparent comparator
// (https://stackoverflow.com/questions/20317413/what-are-transparent-comparators) and store it as a
// std::string but since these are constants, a string_view works just fine.
static const std::set<std::string_view> always_ignore =
{"mysql", "information_schema", "performance_schema", "sys"};

bool SchemaRouterSession::ignore_duplicate_table(std::string_view data) const
{
    bool rval = false;

    std::string_view db = data.substr(0, data.find("."));

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
enum showdb_response SchemaRouterSession::parse_mapping_response(SRBackend* bref, const mxs::Reply& reply)
{
    enum showdb_response rval = SHOWDB_FATAL_ERROR;

    if (reply.error())
    {
        MXB_INFO("Mapping query returned an error; ignoring server '%s': %s",
                 bref->name(), reply.error().message().c_str());
        rval = SHOWDB_FULL_RESPONSE;
    }
    else
    {
        bool duplicate_found = false;
        rval = reply.is_complete() ? SHOWDB_FULL_RESPONSE : SHOWDB_PARTIAL_RESPONSE;

        for (const auto& row : reply.row_data())
        {
            mxb_assert(row.size() == 2);

            if (!row.empty() && !row[0].empty())
            {
                std::string db(row[0]);
                std::string tbl(row[1]);
                mxs::Target* target = bref->target();
                MXB_SINFO("<" << target->name() << ", " << db << ", " << tbl << ">");
                m_shard.add_location(std::move(db), std::move(tbl), target);
            }
        }
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
    MXB_INFO("Mapping databases");

    for (const auto& b : m_backends)
    {
        b->set_mapped(false);
    }

    mxb_assert((m_state & INIT_MAPPING) == 0);

    m_state |= INIT_MAPPING;
    m_state &= ~INIT_UNINT;

    // It is important that the result also contains all the database names with the table set to an empty
    // string. This causes a table with no name to be inserted into the resulting map that represent the
    // database itself. With it, the shard map finds both databases and tables using the same code.
    // The query in question will end up generating duplicate rows as we only select the lowercase forms. This
    // is fine since the schemarouter transforms the names into lowercase before doing a lookup. The problem
    // of double insertion (MXS-4092) is avoided by storing the targets in a set.
    GWBUF buffer = mariadb::create_query("SELECT LOWER(t.table_schema), LOWER(t.table_name) FROM information_schema.tables t "
                                         "UNION ALL "
                                         "SELECT LOWER(s.schema_name), '' FROM information_schema.schemata s ");
    buffer.set_type(GWBUF::TYPE_COLLECT_ROWS);

    for (const auto& b : m_backends)
    {
        if (b->in_use() && !b->is_closed() && b->target()->is_usable())
        {
            if (!b->write(buffer.shallow_clone()))
            {
                MXB_ERROR("Failed to write mapping query to '%s'", b->name());
            }
        }
    }
}

/**
 * Check the hashtable for the right backend for this query.
 * @param router Router instance
 * @param client Client router session
 * @param buffer Query to inspect
 * @return Name of the backend or NULL if the query contains no known databases.
 */
mxs::Target* SchemaRouterSession::get_shard_target(const GWBUF& buffer, uint32_t qtype)
{
    mxs::Target* rval = NULL;
    mxs::sql::OpCode op = mxs::sql::OP_UNDEFINED;
    uint8_t command = mxs_mysql_get_command(buffer);

    if (command == MXS_COM_QUERY)
    {
        op = parser().get_operation(const_cast<GWBUF&>(buffer));
        rval = get_query_target(buffer);
    }

    if (mxs_mysql_is_ps_command(command)
        || Parser::type_mask_contains(qtype, mxs::sql::TYPE_PREPARE_NAMED_STMT)
        || Parser::type_mask_contains(qtype, mxs::sql::TYPE_DEALLOC_PREPARE)
        || Parser::type_mask_contains(qtype, mxs::sql::TYPE_PREPARE_STMT)
        || op == mxs::sql::OP_EXECUTE)
    {
        rval = get_ps_target(buffer, qtype, op);
    }

    if (!buffer.hints.empty() && buffer.hints[0].type == Hint::Type::ROUTE_TO_NAMED_SERVER)
    {
        const char* hinted_server = buffer.hints[0].data.c_str();
        for (const auto& b : m_backends)
        {
            // TODO: What if multiple servers have same name when case-compared?
            if (strcasecmp(b->name(), hinted_server) == 0)
            {
                rval = b->target();
                MXB_INFO("Routing hint found (%s)", rval->name());
            }
        }
    }

    if (rval == NULL && m_current_db.length())
    {
        /**
         * If the target name has not been found and the session has an
         * active database, set is as the target
         */
        rval = get_location(m_current_db);

        if (rval)
        {
            MXB_INFO("Using active database '%s' on '%s'",
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
    if (Parser::type_mask_contains(qtype, mxs::sql::TYPE_SESSION_WRITE)
        || Parser::type_mask_contains(qtype, mxs::sql::TYPE_GSYSVAR_WRITE)
        || Parser::type_mask_contains(qtype, mxs::sql::TYPE_USERVAR_WRITE)
        || Parser::type_mask_contains(qtype, mxs::sql::TYPE_ENABLE_AUTOCOMMIT)
        || Parser::type_mask_contains(qtype, mxs::sql::TYPE_DISABLE_AUTOCOMMIT))
    {
        /** hints don't affect on routing */
        target = TARGET_ALL;
    }
    else if (Parser::type_mask_contains(qtype, mxs::sql::TYPE_SYSVAR_READ)
             || Parser::type_mask_contains(qtype, mxs::sql::TYPE_GSYSVAR_READ))
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
    std::set<std::string> db_names;

    for (auto db : m_shard.get_content())
    {
        db_names.insert(db.first);
    }

    std::unique_ptr<ResultSet> set = ResultSet::create({"Database"});

    for (const auto& name : db_names)
    {
        set->add_row({name});
    }

    set_response(set->as_buffer());
}

mxs::Target* SchemaRouterSession::get_query_target(const GWBUF& buffer)
{
    std::vector<Parser::TableName> table_names = parser().get_table_names(const_cast<GWBUF&>(buffer));

    // We get Parser::TableNames, but as we need qualified names we need to
    // copy them over to a vector<string>.
    std::vector<std::string> tables;
    tables.reserve(table_names.size());

    for (const auto& tn : table_names)
    {
        std::string table = !tn.db.empty() ? std::string(tn.db) : m_current_db;
        table += ".";
        table += tn.table;

        tables.emplace_back(table);
    }

    mxs::Target* rval = NULL;

    std::vector<std::string_view> table_views;
    for (const auto& t : tables)
    {
        // Then we need to copy the modified strings back to our vector<string_view>.
        table_views.emplace_back(std::string_view(t));
    }

    if ((rval = get_location(table_views)))
    {
        MXB_INFO("Query targets table on server '%s'", rval->name());
    }
    else if ((rval = get_location(parser().get_database_names(const_cast<GWBUF&>(buffer)))))
    {
        MXB_INFO("Query targets database on server '%s'", rval->name());
    }

    return rval;
}

mxs::Target* SchemaRouterSession::get_ps_target(const GWBUF& buffer, uint32_t qtype, mxs::sql::OpCode op)
{
    mxs::Target* rval = NULL;
    uint8_t command = mxs_mysql_get_command(buffer);
    GWBUF* bufptr = const_cast<GWBUF*>(&buffer);

    if (Parser::type_mask_contains(qtype, mxs::sql::TYPE_PREPARE_NAMED_STMT))
    {
        // If pStmt is null, the PREPARE was malformed. In that case it can be routed to any backend to get
        // a proper error response. Also returns null if preparing from a variable. This is a limitation.
        GWBUF* pStmt = parser().get_preparable_stmt(*bufptr);
        if (pStmt)
        {
            std::string_view stmt = parser().get_prepare_name(*bufptr);

            if ((rval = get_location(parser().get_table_names(*pStmt))))
            {
                MXB_INFO("PREPARING NAMED %.*s ON SERVER %s", (int)stmt.length(), stmt.data(), rval->name());
                m_shard.add_statement(stmt, rval);
            }
        }
    }
    else if (op == mxs::sql::OP_EXECUTE)
    {
        std::string_view stmt = parser().get_prepare_name(*bufptr);
        mxs::Target* ps_target = m_shard.get_statement(stmt);
        if (ps_target)
        {
            rval = ps_target;
            MXB_INFO("Executing named statement %.*s on server %s",
                     (int)stmt.length(), stmt.data(), rval->name());
        }
    }
    else if (Parser::type_mask_contains(qtype, mxs::sql::TYPE_DEALLOC_PREPARE))
    {
        std::string_view stmt = parser().get_prepare_name(*bufptr);
        if ((rval = m_shard.get_statement(stmt)))
        {
            MXB_INFO("Closing named statement %.*s on server %s",
                     (int)stmt.length(), stmt.data(), rval->name());
            m_shard.remove_statement(stmt);
        }
    }
    else if (Parser::type_mask_contains(qtype, mxs::sql::TYPE_PREPARE_STMT))
    {
        rval = get_location(parser().get_table_names(*bufptr));

        if (rval)
        {
            mxb_assert(buffer.id() != 0);
            m_shard.add_statement(buffer.id(), rval);
        }

        MXB_INFO("Prepare statement on server %s", rval ? rval->name() : "<no target found>");
    }
    else if (mxs_mysql_is_ps_command(command))
    {
        uint32_t id = mxs_mysql_extract_ps_id(&buffer);
        rval = m_shard.get_statement(id);

        if (command == MXS_COM_STMT_CLOSE)
        {
            MXB_INFO("Closing prepared statement %d ", id);
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
