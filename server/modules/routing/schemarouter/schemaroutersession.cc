/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
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
bool detect_show_shards(const Parser& parser, const GWBUF& query);

bool is_ps(uint8_t command, uint32_t type, mxs::sql::OpCode op)
{
    return mxs_mysql_is_ps_command(command)
           || Parser::type_mask_contains(type, mxs::sql::TYPE_PREPARE_NAMED_STMT)
           || Parser::type_mask_contains(type, mxs::sql::TYPE_DEALLOC_PREPARE)
           || Parser::type_mask_contains(type, mxs::sql::TYPE_PREPARE_STMT)
           || op == mxs::sql::OP_EXECUTE;
}

SchemaRouterSession::SchemaRouterSession(MXS_SESSION* session,
                                         SchemaRouter* router,
                                         SRBackendList backends)
    : mxs::RouterSession(session)
    , m_client(static_cast<MariaDBClientConnection*>(session->client_connection()))
    , m_backends(std::move(backends))
    , m_config(*router->m_config.values())
    , m_router(router)
    , m_key(get_cache_key())
    , m_state(0)
    , m_load_target(NULL)
    , m_qc(parser(), this, session, TYPE_ALL)
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
}

SRBackend* SchemaRouterSession::resolve_query_target(const GWBUF& packet, uint32_t type,
                                                     uint8_t command, enum route_target route_target)
{
    mxs::Target* target = get_shard_target(packet, type);

    if (target && target->is_usable())
    {
        return get_shard_backend(target->name());
    }

    if (command == MXS_COM_FIELD_LIST || m_current_db.empty() || TARGET_IS_ANY(route_target))
    {
        /** No current database and no databases in query or the database is
         * ignored, route to first available backend. */

        if (SRBackend* b = get_any_backend())
        {
            return b;
        }
    }

    /**No valid backends alive*/
    MXB_ERROR("Failed to route query, no backends are available.");
    return nullptr;
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

bool SchemaRouterSession::tables_are_on_all_nodes(const std::set<mxs::Target*>& candidates) const
{
    size_t valid_nodes = 0;
    size_t nodes_in_candidates = 0;

    for (const auto& b : m_backends)
    {
        if (b->in_use())
        {
            ++valid_nodes;

            if (candidates.count(b->target()))
            {
                ++nodes_in_candidates;
            }
        }
    }

    return valid_nodes == nodes_in_candidates;
}

// Returns true if the query was queued because a wait is needed. Returns false if a shard mapping was
// acquired and no waiting is needed: in this case the query should continue to be routed normally.
bool SchemaRouterSession::wait_for_shard(GWBUF& packet)
{
    if (m_shard.empty() && (m_state & INIT_MAPPING) == 0)
    {
        if (m_dcid)
        {
            // The delayed call is already in place, let it take care of the shard update
            m_queue.push_back(packet.shallow_clone());
            return true;
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
                // Try and see if we can find an acceptably stale entry. The update is already in progress but
                // waiting for it is not desirable as it would block all requests by this user.
                m_shard = m_router->m_shard_manager.get_stale_shard(
                    m_key, m_config.refresh_interval.count(), m_config.max_staleness.count());

                if (m_shard.empty())
                {
                    // Wait for the other session to finish its update and reuse that result
                    mxb_assert(m_dcid == 0);
                    m_queue.push_back(packet.shallow_clone());

                    auto worker = mxs::RoutingWorker::get_current();
                    m_dcid = m_pSession->dcall(1000ms, &SchemaRouterSession::delay_routing, this);
                    MXB_INFO("Waiting for the database mapping to be completed by another session");

                    return true;
                }
            }
        }
    }

    return false;
}

bool SchemaRouterSession::wait_for_init(GWBUF& packet)
{
    /**
     * If the databases are still being mapped or if the client connected
     * with a default database but no database mapping was performed we need
     * to store the query. Once the databases have been mapped and/or the
     * default database is taken into use we can send the query forward.
     */
    bool busy = m_state & (INIT_MAPPING | INIT_USE_DB);

    if (busy)
    {
        m_queue.push_back(packet.shallow_clone());

        if (m_state == (INIT_READY | INIT_USE_DB))
        {
            /**
             * This state is possible if a client connects with a default database
             * and the shard map was found from the router cache
             */
            handle_default_db();
        }
    }

    return busy;
}

bool SchemaRouterSession::routeQuery(GWBUF&& packet)
{
    if (m_load_target)
    {
        SRBackend* bref = get_shard_backend(m_load_target->name());

        if (!bref)
        {
            return false;
        }

        MXB_INFO("Route LOAD DATA to \t%s <", bref->name());
        return bref->write(std::move(packet), mxs::Backend::NO_RESPONSE);
    }

    if (wait_for_shard(packet) || wait_for_init(packet))
    {
        return true;
    }

    bool ret = false;
    const auto& info = m_qc.update_and_commit_route_info(packet);
    uint8_t command = info.command();
    uint32_t type = info.type_mask();
    mxs::sql::OpCode op = parser().get_operation(packet);
    enum route_target route_target = TARGET_UNDEFINED;

    if (!is_ps(command, type, op) && m_qc.target_is_all(info.target()))
    {
        route_target = TARGET_ALL;
    }

    /** Create the response to the SHOW DATABASES from the mapped databases */
    if (op == mxs::sql::OP_SHOW_DATABASES)
    {
        send_databases();
        return 1;
    }
    else if (detect_show_shards(parser(), packet))
    {
        send_shards();
        return 1;
    }

    // Route all transaction control commands to all backends. This will keep the transaction state
    // consistent even if no default database is used or if the default database being used is located
    // on more than one node.
    if (packet.hints().empty()
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
                m_qc.revert_update();
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
        ret = route_session_write(std::move(packet), command);
    }
    else if (SRBackend* bref = resolve_query_target(packet, type, command, route_target))
    {
        // Store the target we're routing the query to. This can be used later if a situation is
        // encountered where a query has no "natural" target (e.g. CREATE DATABASE with no default
        // database).
        m_prev_target = bref;

        MXB_INFO("Route query to \t%s <", bref->name());

        auto responds = protocol_data().will_respond(packet) ?
            mxs::Backend::EXPECT_RESPONSE : mxs::Backend::NO_RESPONSE;

        ret = bref->write(std::move(packet), responds);
    }
    else
    {
        MXB_ERROR("Could not find valid server for %s, closing connection.",
                  mariadb::cmd_to_string(packet));
    }

    return ret;
}
void SchemaRouterSession::handle_mapping_reply(SRBackend* bref, const mxs::Reply& reply)
{
    if (inspect_mapping_states(bref, reply) == 1)
    {
        synchronize_shards();
        m_state &= ~INIT_MAPPING;

        /* Check if the session is reconnecting with a database name
         * that is not in the hashtable. If the database is not found
         * then close the session. */

        if (m_state & INIT_USE_DB)
        {
            handle_default_db();
        }
        else if (m_queue.size())
        {
            mxb_assert(m_state == INIT_READY || m_state == INIT_USE_DB);
            MXB_INFO("Routing stored query");
            route_queued_query();
        }
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
    SRBackend* bref = static_cast<SRBackend*>(down.endpoint()->get_userdata());

    const auto& error = reply.error();

    if (error.is_unexpected_error())
    {
        // All unexpected errors are related to server shutdown.
        MXB_SINFO("Server '" << bref->name() << "' is shutting down");

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

    // If this is a LOAD DATA LOCAL INFILE command, stream the data to this backend until it completes.
    m_load_target = reply.state() == mxs::ReplyState::LOAD_DATA ? bref->target() : nullptr;
    m_qc.update_from_reply(reply);

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
    uint8_t command = mariadb::get_command(buf);

    if (command == MXS_COM_QUERY && parser.get_operation(const_cast<GWBUF&>(buf)) == mxs::sql::OP_CHANGE_DB)
    {
        auto tokens = mxb::strtok(parser.get_sql(buf), "` \n\t;");

        if (tokens.size() < 2 || strcasecmp(tokens[0].c_str(), "use") != 0)
        {
            MXB_INFO("extract_database: Malformed chage database packet.");
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

    if (protocol_data().will_respond(buffer))
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
        if (b->in_use())
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
 */
void SchemaRouterSession::send_shards()
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
void SchemaRouterSession::handle_default_db()
{
    bool rval = false;

    for (auto target : m_shard.get_all_locations(m_connect_db, ""))
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
        MXB_INFO("Connecting to a non-existent database '%s'", m_connect_db.c_str());
        char errmsg[128 + MYSQL_DATABASE_MAXLEN + 1];
        sprintf(errmsg, "Unknown database '%s'", m_connect_db.c_str());
        if (m_config.debug)
        {
            sprintf(errmsg + strlen(errmsg), " ([%" PRIu64 "]: DB not found on connect)", m_pSession->id());
        }

        m_pSession->kill(errmsg);
    }
}

void SchemaRouterSession::route_queued_query()
{
    GWBUF tmp = std::move(m_queue.front());
    m_queue.pop_front();

    MXB_INFO("Routing queued query: %s", get_sql_string(tmp).c_str());

    m_pSession->delay_routing(this, std::move(tmp), 0ms);
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
            m_pSession->kill("Error: database mapping failed due to: " + reply.error().message());
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
                m_pSession->kill("Error: duplicate tables found on two different shards.");
            }
        }
    }

    return std::all_of(
        m_backends.begin(), m_backends.end(), [](const auto& bref) {
        return !bref->in_use() || bref->is_mapped();
    });
}

/**
 * Read new database name from COM_INIT_DB packet or a literal USE ... COM_QUERY
 * packet and change the default database on the relevant backends.
 *
 * @param buf Buffer containing the database change query
 * @param cmd The command being executed
 *
 * @return True if new database was set and a query was executed, false if non-existent database was tried
 *         to be used and it wasn't found on any of the backends.
 */
bool SchemaRouterSession::change_current_db(const GWBUF& buf, uint8_t cmd)
{
    bool succp = false;
    auto [ok, db] = extract_database(parser(), buf);

    if (ok)
    {
        auto targets = m_shard.get_all_locations(db, "");
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
 * Parses a response to the database mapping query
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
 * Sends the database mapping query to all backends
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
        if (b->in_use() && b->target()->is_usable())
        {
            if (!b->write(buffer.shallow_clone()))
            {
                MXB_ERROR("Failed to write mapping query to '%s'", b->name());
            }
        }
    }
}

/**
 * Find the correct target for the query
 *
 * @param buffer Query to inspect
 * @param qtype  The query type mask
 *
 * @return The target if one was found, otherwise nullptr
 */
mxs::Target* SchemaRouterSession::get_shard_target(const GWBUF& buffer, uint32_t qtype)
{
    mxs::Target* rval = NULL;
    mxs::sql::OpCode op = mxs::sql::OP_UNDEFINED;
    uint8_t command = mariadb::get_command(buffer);

    if (command == MXS_COM_QUERY)
    {
        op = parser().get_operation(const_cast<GWBUF&>(buffer));
        rval = get_query_target(buffer);
    }

    if (is_ps(command, qtype, op))
    {
        rval = get_ps_target(buffer, qtype, op);
    }

    if (!buffer.hints().empty() && buffer.hints()[0].type == Hint::Type::ROUTE_TO_NAMED_SERVER)
    {
        const char* hinted_server = buffer.hints()[0].data.c_str();
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
        rval = get_location(m_current_db, "");

        if (rval)
        {
            MXB_INFO("Using active database '%s' on '%s'",
                     m_current_db.c_str(),
                     rval->name());
        }
    }
    return rval;
}

SRBackend* SchemaRouterSession::get_shard_backend(std::string_view name)
{
    for (const auto& b : m_backends)
    {
        if (b->in_use() && mxb::sv_case_eq(name, b->target()->name()) && b->target()->is_usable())
        {
            return b.get();
        }
    }

    return nullptr;
}

/**
 * Generates a custom SHOW DATABASES result set from all the databases in the
 * hashtable.
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

mxs::Target*
SchemaRouterSession::get_query_target_from_locations(const std::set<mxs::Target*>& locations)
{
    // TODO: const-correct, get_location() is non-const right now.
    mxs::Target* rval = nullptr;

    if (!m_current_db.empty() && tables_are_on_all_nodes(locations))
    {
        rval = get_location(m_current_db, "");
        MXB_INFO("Query target table is on all nodes, using node with current default database '%s'",
                 m_current_db.c_str());
        mxb_assert_message(rval, "If table is on all nodes, it should always have a location.");
    }
    else
    {
        rval = get_valid_target(locations);
    }

    return rval;
}

mxs::Target* SchemaRouterSession::get_query_target(const GWBUF& buffer)
{
    std::vector<Parser::TableName> table_names = parser().get_table_names(const_cast<GWBUF&>(buffer));

    for (auto& tn : table_names)
    {
        if (tn.db.empty())
        {
            // Use the current default db as the database of any query that does not explicitly define one.
            tn.db = m_current_db;
        }
    }

    auto locations = m_shard.get_all_locations(table_names);
    mxs::Target* rval = get_query_target_from_locations(locations);

    if (!rval)
    {
        // No matching table found. Try to match based on the database name.
        table_names.clear();

        for (const auto& db : parser().get_database_names(buffer))
        {
            table_names.emplace_back(db, "");
        }

        rval = get_query_target_from_locations(m_shard.get_all_locations(table_names));
    }

    if (rval)
    {
        MXB_INFO("Query targets server '%s'", rval->name());
    }

    return rval;
}

mxs::Target* SchemaRouterSession::get_ps_target(const GWBUF& buffer, uint32_t qtype, mxs::sql::OpCode op)
{
    mxs::Target* rval = NULL;
    uint8_t command = mariadb::get_command(buffer);

    if (Parser::type_mask_contains(qtype, mxs::sql::TYPE_PREPARE_NAMED_STMT))
    {
        // If pStmt is null, the PREPARE was malformed. In that case it can be routed to any backend to get
        // a proper error response. Also returns null if preparing from a variable. This is a limitation.
        GWBUF* pStmt = parser().get_preparable_stmt(buffer);
        if (pStmt)
        {
            std::string_view stmt = parser().get_prepare_name(buffer);

            if ((rval = get_location(parser().get_table_names(*pStmt))))
            {
                MXB_INFO("PREPARING NAMED %.*s ON SERVER %s", (int)stmt.length(), stmt.data(), rval->name());
                m_shard.add_statement(stmt, rval);
            }
        }
    }
    else if (mxs_mysql_is_ps_command(command))
    {
        uint32_t id = mxs_mysql_extract_ps_id(buffer);
        rval = m_shard.get_statement(id);

        if (command == MXS_COM_STMT_CLOSE)
        {
            MXB_INFO("Closing prepared statement %d ", id);
            m_shard.remove_statement(id);
        }
    }
    else if (op == mxs::sql::OP_EXECUTE)
    {
        std::string_view stmt = parser().get_prepare_name(buffer);
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
        std::string_view stmt = parser().get_prepare_name(buffer);
        if ((rval = m_shard.get_statement(stmt)))
        {
            MXB_INFO("Closing named statement %.*s on server %s",
                     (int)stmt.length(), stmt.data(), rval->name());
            m_shard.remove_statement(stmt);
        }
    }
    else if (Parser::type_mask_contains(qtype, mxs::sql::TYPE_PREPARE_STMT))
    {
        rval = get_location(parser().get_table_names(buffer));

        if (rval)
        {
            mxb_assert(buffer.id() != 0);
            m_shard.add_statement(buffer.id(), rval);
        }

        MXB_INFO("Prepare statement on server %s", rval ? rval->name() : "<no target found>");
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
