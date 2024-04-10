/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/queryclassifier.hh>
#include <unordered_map>
#include <maxbase/alloc.hh>
#include <maxbase/string.hh>

using mariadb::QueryClassifier;
using mxs::Parser;

namespace
{

const int QC_TRACE_MSG_LEN = 1000;

struct DummyHandler final : public mariadb::QueryClassifier::Handler
{
    bool lock_to_master() override
    {
        return true;
    }

    bool is_locked_to_master() const override
    {
        return false;
    }

    bool supports_hint(Hint::Type hint_type) const override
    {
        return false;
    }
};

static DummyHandler dummy_handler;

std::string get_current_db(MXS_SESSION* session)
{
    return session->client_connection()->current_db();
}

uint32_t get_prepare_type(const mxs::Parser& parser, const GWBUF& buffer)
{
    uint32_t type = mxs::sql::TYPE_UNKNOWN;

    if (parser.is_prepare(buffer))
    {
        type = parser.get_type_mask(buffer) & ~mxs::sql::TYPE_PREPARE_STMT;
    }
    else
    {
        GWBUF* stmt = parser.get_preparable_stmt(buffer);

        if (stmt)
        {
            type = parser.get_type_mask(*stmt);
        }
    }

    return type;
}

std::string get_text_ps_id(const mxs::Parser& parser, const GWBUF& buffer)
{
    return std::string(parser.get_prepare_name(buffer));
}

bool foreach_table(QueryClassifier& qc,
                   MXS_SESSION* pSession,
                   const GWBUF& querybuf,
                   bool (* func)(QueryClassifier& qc, const std::string&))
{
    bool rval = true;

    for (const auto& t : qc.parser().get_table_names(querybuf))
    {
        std::string table;

        if (t.db.empty())
        {
            table = get_current_db(pSession);
        }
        else
        {
            table = t.db;
        }

        table += ".";
        table += t.table;

        if (!func(qc, table))
        {
            rval = false;
            break;
        }
    }

    return rval;
}
}

namespace mariadb
{

class QueryClassifier::PSManager
{
    PSManager(const PSManager&) = delete;
    PSManager& operator=(const PSManager&) = delete;

public:
    struct PreparedStmt
    {
        uint32_t type = 0;
        uint16_t param_count = 0;
        bool     route_to_last_used = false;
    };

    struct BinaryPreparedStmt
    {
        uint32_t     id;
        PreparedStmt ps;

        explicit BinaryPreparedStmt(uint32_t ps_id)
            : id(ps_id)
        {
        }

        bool operator==(const BinaryPreparedStmt& other) const
        {
            return id == other.id;
        }
    };

    PSManager(mxs::Parser& parser, Log log)
        : m_parser(parser)
        , m_log(log)
    {
    }

    ~PSManager()
    {
    }

    void store(const GWBUF& buffer, uint32_t id)
    {
        bool is_prepare = m_parser.is_prepare(buffer);

        mxb_assert(is_prepare
                   || Parser::type_mask_contains(m_parser.get_type_mask(buffer),
                                                 mxs::sql::TYPE_PREPARE_NAMED_STMT));
        if (is_prepare)
        {
            BinaryPreparedStmt stmt(id);
            stmt.ps.type = get_prepare_type(m_parser, buffer);
            stmt.ps.route_to_last_used = m_parser.relates_to_previous(buffer);

            m_binary_ps.emplace_back(std::move(stmt));
        }
        else if (m_parser.is_query(buffer))
        {
            PreparedStmt stmt;
            stmt.type = get_prepare_type(m_parser, buffer);
            stmt.route_to_last_used = m_parser.relates_to_previous(buffer);

            m_text_ps.emplace(get_text_ps_id(m_parser, buffer), std::move(stmt));
        }
        else
        {
            mxb_assert(!true);
        }
    }

    const PreparedStmt* get(uint32_t id) const
    {
        const PreparedStmt* rval = nullptr;
        auto it = std::find(m_binary_ps.begin(), m_binary_ps.end(), BinaryPreparedStmt(id));

        if (it != m_binary_ps.end())
        {
            rval = &it->ps;
        }
        else if (m_parser.is_execute_immediately_ps(id))
        {
            if (m_log == Log::ALL)
            {
                auto msg = MAKE_STR("Using unknown binary prepared statement with ID " << id);
                mxs::unexpected_situation(msg.c_str());
                MXB_WARNING("%s", msg.c_str());
            }
        }

        return rval;
    }

    const PreparedStmt* get(std::string id) const
    {
        const PreparedStmt* rval = nullptr;
        auto it = m_text_ps.find(id);

        if (it != m_text_ps.end())
        {
            rval = &it->second;
        }
        else if (m_log == Log::ALL)
        {
            auto msg = MAKE_STR("Using unknown text prepared statement with ID '" << id << "'");
            mxs::unexpected_situation(msg.c_str());
            MXB_WARNING("%s", msg.c_str());
        }

        return rval;
    }

    void erase(std::string id)
    {
        if (m_text_ps.erase(id) == 0)
        {
            if (m_log == Log::ALL)
            {
                auto msg = MAKE_STR("Closing unknown text prepared statement with ID '" << id << "'");
                mxs::unexpected_situation(msg.c_str());
                MXB_WARNING("%s", msg.c_str());
            }
        }
    }

    void erase(uint32_t id)
    {
        auto it = std::find(m_binary_ps.begin(), m_binary_ps.end(), BinaryPreparedStmt(id));

        if (it != m_binary_ps.end())
        {
            m_binary_ps.erase(it);
        }
        else if (m_log == Log::ALL)
        {
            auto msg = MAKE_STR("Closing unknown binary prepared statement with ID " << id);
            mxs::unexpected_situation(msg.c_str());
            MXB_WARNING("%s", msg.c_str());
        }
    }

    void erase(const GWBUF& buffer)
    {
        if (m_parser.is_query(buffer))
        {
            erase(get_text_ps_id(m_parser, buffer));
        }
        else if (m_parser.is_ps_packet(buffer))
        {
            erase(m_parser.get_ps_id(buffer));
        }
        else
        {
            mxb_assert_message(!true, "QueryClassifier::PSManager::erase called with invalid query");
        }
    }

    void set_param_count(uint32_t id, uint16_t param_count)
    {
        auto it = std::find(m_binary_ps.begin(), m_binary_ps.end(), BinaryPreparedStmt(id));
        mxb_assert(it != m_binary_ps.end());

        if (it != m_binary_ps.end())
        {
            it->ps.param_count = param_count;
        }
    }

    uint16_t param_count(uint32_t id) const
    {
        uint16_t rval = 0;
        auto it = std::find(m_binary_ps.begin(), m_binary_ps.end(), BinaryPreparedStmt(id));

        if (it != m_binary_ps.end())
        {
            rval = it->ps.param_count;
        }

        return rval;
    }

private:
    mxs::Parser&                                  m_parser;
    std::vector<BinaryPreparedStmt>               m_binary_ps;
    std::unordered_map<std::string, PreparedStmt> m_text_ps;
    Log                                           m_log;
};

//
// QueryClassifier
//

QueryClassifier::QueryClassifier(mxs::Parser& parser, MXS_SESSION* pSession)
    : QueryClassifier(parser, &dummy_handler, pSession, TYPE_ALL, Log::NONE)
{
    m_verbose = false;
}

QueryClassifier::QueryClassifier(mxs::Parser& parser,
                                 Handler* pHandler,
                                 MXS_SESSION* pSession,
                                 mxs_target_t use_sql_variables_in,
                                 Log log)
    : m_parser(parser)
    , m_pHandler(pHandler)
    , m_pSession(pSession)
    , m_use_sql_variables_in(use_sql_variables_in)
    , m_multi_statements_allowed(pSession->protocol_data()->are_multi_statements_allowed())
    , m_sPs_manager(new PSManager(parser, log))
{
}

void QueryClassifier::ps_store(const GWBUF& buffer, uint32_t id)
{
    m_prev_ps_id = id;
    return m_sPs_manager->store(buffer, id);
}

void QueryClassifier::ps_erase(const GWBUF& buffer)
{
    if (m_parser.is_ps_packet(buffer))
    {
        // Erase the type of the statement stored with the internal ID
        m_sPs_manager->erase(ps_id_internal_get(buffer));
    }
    else
    {
        // Not a PS command, we don't need the ID mapping
        m_sPs_manager->erase(buffer);
    }
}

bool QueryClassifier::query_type_is_read_only(uint32_t qtype) const
{
    bool rval = false;

    if (!Parser::type_mask_contains(qtype, mxs::sql::TYPE_MASTER_READ)
        && !Parser::type_mask_contains(qtype, mxs::sql::TYPE_WRITE)
        && (Parser::type_mask_contains(qtype, mxs::sql::TYPE_READ)
            || Parser::type_mask_contains(qtype, mxs::sql::TYPE_USERVAR_READ)
            || Parser::type_mask_contains(qtype, mxs::sql::TYPE_SYSVAR_READ)
            || Parser::type_mask_contains(qtype, mxs::sql::TYPE_GSYSVAR_READ)))
    {
        if (Parser::type_mask_contains(qtype, mxs::sql::TYPE_USERVAR_READ))
        {
            if (m_use_sql_variables_in == TYPE_ALL)
            {
                rval = true;
            }
        }
        else
        {
            rval = true;
        }
    }

    return rval;
}

void QueryClassifier::process_routing_hints(const GWBUF::HintVector& hints, uint32_t* target)
{
    using Type = Hint::Type;
    const char max_rlag[] = "max_slave_replication_lag";

    bool check_more = true;
    for (auto it = hints.begin(); check_more && it != hints.end(); it++)
    {
        const Hint& hint = *it;
        if (m_pHandler->supports_hint(hint.type))
        {
            switch (hint.type)
            {
            case Type::ROUTE_TO_MASTER:
                // This means override, so we bail out immediately.
                *target = TARGET_MASTER;
                check_more = false;
                break;

            case Type::ROUTE_TO_NAMED_SERVER:
                // The router is expected to look up the named server.
                *target |= TARGET_NAMED_SERVER;
                break;

            case Type::ROUTE_TO_UPTODATE_SERVER:
                // TODO: Add generic target type, never to be seem by RWS.
                mxb_assert(false);
                break;

            case Type::ROUTE_TO_ALL:
                // TODO: Add generic target type, never to be seem by RWS.
                mxb_assert(false);
                break;

            case Type::ROUTE_TO_LAST_USED:
                *target = TARGET_LAST_USED;
                break;

            case Type::PARAMETER:
                if (strncasecmp(hint.data.c_str(), max_rlag, sizeof(max_rlag) - 1) == 0)
                {
                    *target |= TARGET_RLAG_MAX;
                }
                else
                {
                    MXB_ERROR("Unknown hint parameter '%s' when '%s' was expected.",
                              hint.data.c_str(), max_rlag);
                }
                break;

            case Type::ROUTE_TO_SLAVE:
                *target = TARGET_SLAVE;
                break;

            case Type::NONE:
                mxb_assert(!true);
                break;
            }
        }
    }
}

uint32_t QueryClassifier::get_route_target(uint32_t qtype, const TrxTracker& trx_tracker)
{
    bool trx_active = trx_tracker.is_trx_active();
    uint32_t target = TARGET_UNDEFINED;
    bool load_active = m_route_info.load_data_active();
    mxb_assert(!load_active);

    /**
     * Prepared statements preparations should go to all servers
     */
    if (Parser::type_mask_contains(qtype, mxs::sql::TYPE_PREPARE_STMT)
        || Parser::type_mask_contains(qtype, mxs::sql::TYPE_PREPARE_NAMED_STMT))
    {
        target = TARGET_ALL;
    }
    /**
     * Either SET TRANSACTION READ ONLY or SET TRANSACTION READ WRITE. They need to be treated as a write as
     * it only modifies the behavior of the next START TRANSACTION statement. As such, it is routed exactly
     * like a normal transaction except that the router is responsible for injecting the SET TRANSACTION
     * command if a reconnection takes place.
     */
    else if (Parser::type_mask_contains(qtype, mxs::sql::TYPE_NEXT_TRX))
    {
        target = TARGET_MASTER;
    }
    /**
     * These queries should be routed to all servers
     */
    else if (!load_active && !Parser::type_mask_contains(qtype, mxs::sql::TYPE_WRITE)
             && (Parser::type_mask_contains(qtype, mxs::sql::TYPE_SESSION_WRITE)
                 ||     /** Configured to allow writing user variables to all nodes */
                 (m_use_sql_variables_in == TYPE_ALL
                  && Parser::type_mask_contains(qtype, mxs::sql::TYPE_USERVAR_WRITE))
                 || Parser::type_mask_contains(qtype, mxs::sql::TYPE_GSYSVAR_WRITE)
                 ||     /** enable or disable autocommit are always routed to all */
                 Parser::type_mask_contains(qtype, mxs::sql::TYPE_ENABLE_AUTOCOMMIT)
                 || Parser::type_mask_contains(qtype, mxs::sql::TYPE_DISABLE_AUTOCOMMIT)))
    {
        target |= TARGET_ALL;
    }
    /**
     * Hints may affect on routing of the following queries
     */
    else if (!trx_active && !load_active && query_type_is_read_only(qtype))
    {
        target = TARGET_SLAVE;
    }
    else if (trx_tracker.is_trx_read_only())
    {
        /* Force TARGET_SLAVE for READ ONLY transaction (active or ending) */
        target = TARGET_SLAVE;
    }
    else
    {
        mxb_assert(trx_active || load_active
                   || (Parser::type_mask_contains(qtype, mxs::sql::TYPE_WRITE)
                       || Parser::type_mask_contains(qtype, mxs::sql::TYPE_MASTER_READ)
                       || Parser::type_mask_contains(qtype, mxs::sql::TYPE_SESSION_WRITE)
                       || (Parser::type_mask_contains(qtype, mxs::sql::TYPE_USERVAR_READ)
                           && m_use_sql_variables_in == TYPE_MASTER)
                       || (Parser::type_mask_contains(qtype, mxs::sql::TYPE_SYSVAR_READ)
                           && m_use_sql_variables_in == TYPE_MASTER)
                       || (Parser::type_mask_contains(qtype, mxs::sql::TYPE_GSYSVAR_READ)
                           && m_use_sql_variables_in == TYPE_MASTER)
                       || (Parser::type_mask_contains(qtype, mxs::sql::TYPE_GSYSVAR_WRITE)
                           && m_use_sql_variables_in == TYPE_MASTER)
                       || (Parser::type_mask_contains(qtype, mxs::sql::TYPE_USERVAR_WRITE)
                           && m_use_sql_variables_in == TYPE_MASTER)
                       || Parser::type_mask_contains(qtype, mxs::sql::TYPE_BEGIN_TRX)
                       || Parser::type_mask_contains(qtype, mxs::sql::TYPE_ENABLE_AUTOCOMMIT)
                       || Parser::type_mask_contains(qtype, mxs::sql::TYPE_DISABLE_AUTOCOMMIT)
                       || Parser::type_mask_contains(qtype, mxs::sql::TYPE_ROLLBACK)
                       || Parser::type_mask_contains(qtype, mxs::sql::TYPE_COMMIT)
                       || Parser::type_mask_contains(qtype, mxs::sql::TYPE_EXEC_STMT)
                       || Parser::type_mask_contains(qtype, mxs::sql::TYPE_CREATE_TMP_TABLE)
                       || Parser::type_mask_contains(qtype, mxs::sql::TYPE_UNKNOWN))
                   || Parser::type_mask_contains(qtype, mxs::sql::TYPE_EXEC_STMT));

        target = TARGET_MASTER;
    }

    return target;
}

uint32_t QueryClassifier::ps_id_internal_get(const GWBUF& buffer)
{
    uint32_t id = m_parser.get_ps_id(buffer);

    // Do we implicitly refer to the previous prepared statement.
    if (m_parser.is_ps_direct_exec_id(id) && m_prev_ps_id)
    {
        return m_prev_ps_id;
    }

    return id;
}

void QueryClassifier::log_transaction_status(const GWBUF& querybuf, uint32_t qtype,
                                             const TrxTracker& trx_tracker)
{
    if (m_route_info.multi_part_packet())
    {
        MXB_INFO("> Processing large request with more than 2^24 bytes of data");
    }
    else if (!m_route_info.load_data_active())
    {
        MXB_INFO("> Autocommit: %s, trx is %s, %s",
                 trx_tracker.is_autocommit() ? "[enabled]" : "[disabled]",
                 trx_tracker.is_trx_active() ? "[open]" : "[not open]",
                 session()->protocol()->describe(querybuf).c_str());
    }
    else
    {
        MXB_INFO("> Processing LOAD DATA LOCAL INFILE.");
    }
}

void QueryClassifier::create_tmp_table(const GWBUF& querybuf, uint32_t type)
{
    std::string table;

    for (const auto& t : m_parser.get_table_names(querybuf))
    {
        if (t.db.empty())
        {
            table = get_current_db(session());
        }
        else
        {
            table = t.db;
        }

        table += '.';
        table += t.table;
        break;
    }

    MXB_INFO("Added temporary table %s", table.c_str());

    /** Add the table to the set of temporary tables */
    add_tmp_table(table);
}

bool QueryClassifier::is_read_tmp_table(const GWBUF& querybuf, uint32_t qtype)
{
    bool rval = false;

    if (Parser::type_mask_contains(qtype, mxs::sql::TYPE_READ)
        || Parser::type_mask_contains(qtype, mxs::sql::TYPE_USERVAR_READ)
        || Parser::type_mask_contains(qtype, mxs::sql::TYPE_SYSVAR_READ)
        || Parser::type_mask_contains(qtype, mxs::sql::TYPE_GSYSVAR_READ))
    {
        if (!foreach_table(*this, m_pSession, querybuf, &QueryClassifier::find_table))
        {
            rval = true;
        }
    }

    return rval;
}

/**
 * @brief Handle multi statement queries and load statements
 *
 * One of the possible types of handling required when a request is routed
 *
 * @param qc                   The query classifier
 * @param current_target       The current target
 * @param querybuf             Buffer containing query to be routed
 * @param qtype                Query type
 *
 * @return QueryClassifier::CURRENT_TARGET_MASTER if the session should be fixed
 *         to the master, QueryClassifier::CURRENT_TARGET_UNDEFINED otherwise.
 */
QueryClassifier::current_target_t QueryClassifier::handle_multi_temp_and_load(
    QueryClassifier::current_target_t current_target,
    const GWBUF& querybuf,
    uint32_t* qtype,
    const mxs::Parser::QueryInfo& query_info)
{
    QueryClassifier::current_target_t rv = QueryClassifier::CURRENT_TARGET_UNDEFINED;

    bool is_query = query_info.query;

    /** Check for multi-statement queries. If no master server is available
     * and a multi-statement is issued, an error is returned to the client
     * when the query is routed. */
    if (current_target != QueryClassifier::CURRENT_TARGET_MASTER)
    {
        bool is_multi = is_query && query_info.op == mxs::sql::OP_CALL;
        if (!is_multi && multi_statements_allowed() && is_query)
        {
            is_multi = query_info.multi_stmt;
        }

        if (is_multi)
        {
            rv = QueryClassifier::CURRENT_TARGET_MASTER;
        }
    }

    /**
     * Check if the query has anything to do with temporary tables.
     */
    if (have_tmp_tables() && is_query)
    {
        if (is_read_tmp_table(querybuf, query_info.type_mask))
        {
            *qtype |= mxs::sql::TYPE_MASTER_READ;
        }
    }

    return rv;
}

uint16_t QueryClassifier::get_param_count(uint32_t id)
{
    return m_sPs_manager->param_count(id);
}

bool QueryClassifier::query_continues_ps(const GWBUF& buffer)
{
    return m_parser.continues_ps(buffer, m_route_info.command());
}

const QueryClassifier::RouteInfo&
QueryClassifier::update_route_info(const GWBUF& buffer)
{
    uint32_t route_target = TARGET_MASTER;
    uint32_t type_mask = mxs::sql::TYPE_UNKNOWN;
    bool locked_to_master = m_pHandler->is_locked_to_master();
    current_target_t current_target = locked_to_master ? CURRENT_TARGET_MASTER : CURRENT_TARGET_UNDEFINED;
    TrxTracker& trx_tracker = m_route_info.m_trx_tracker;

    // Stash the current state in case we need to roll it back
    m_prev_route_info = m_route_info;

    auto query_info = m_parser.get_query_info(buffer);
    uint32_t stmt_id = query_info.ps_id;
    uint8_t cmd = query_info.command;

    m_route_info.set_multi_part_packet(query_info.multi_part_packet);

    if (m_route_info.multi_part_packet())
    {
        // Trailing part of a multi-packet query, ignore it
        return m_route_info;
    }

    trx_tracker.track_transaction_state(query_info.type_mask);

    // Reset for every classification
    m_route_info.set_ps_continuation(false);

    // TODO: It may be sufficient to simply check whether we are in a read-only
    // TODO: transaction.
    bool in_read_only_trx =
        (current_target != QueryClassifier::CURRENT_TARGET_UNDEFINED) && trx_tracker.is_trx_read_only();

    if (m_route_info.load_data_active())
    {
        // A LOAD DATA LOCAL INFILE is ongoing
    }
    else if (!query_info.empty)
    {
        if (query_info.ps_direct_exec_id && m_prev_ps_id)
        {
            stmt_id = m_prev_ps_id;
        }

        /**
         * If the session is inside a read-only transaction, we trust that the
         * server acts properly even when non-read-only queries are executed.
         * For this reason, we can skip the parsing of the statement completely.
         */
        if (in_read_only_trx)
        {
            type_mask = mxs::sql::TYPE_READ;
        }
        else
        {
            type_mask = query_info.type_mask;

            current_target = handle_multi_temp_and_load(current_target, buffer, &type_mask, query_info);

            if (current_target == QueryClassifier::CURRENT_TARGET_MASTER)
            {
                /* If we do not have a master node, assigning the forced node is not
                 * effective since we don't have a node to force queries to. In this
                 * situation, assigning mxs::sql::TYPE_WRITE for the query will trigger
                 * the error processing. */
                if (!m_pHandler->lock_to_master())
                {
                    type_mask |= mxs::sql::TYPE_WRITE;
                }
            }
        }

        /**
         * Find out where to route the query. Result may not be clear; it is
         * possible to have a hint for routing to a named server which can
         * be either slave or master.
         * If query would otherwise be routed to slave then the hint determines
         * actual target server if it exists.
         *
         * route_target is a bitfield and may include :
         * TARGET_ALL
         * - route to all connected backend servers
         * TARGET_SLAVE[|TARGET_NAMED_SERVER|TARGET_RLAG_MAX]
         * - route primarily according to hints, then to slave and if those
         *   failed, eventually to master
         * TARGET_MASTER[|TARGET_NAMED_SERVER|TARGET_RLAG_MAX]
         * - route primarily according to the hints and if they failed,
         *   eventually to master
         */

        bool route_to_last_used = false;

        if (locked_to_master)
        {
            /** The session is locked to the master */
            route_target = TARGET_MASTER;
        }
        else
        {
            bool is_query = query_info.query;

            if (!in_read_only_trx
                && is_query
                && query_info.op == mxs::sql::OP_EXECUTE)
            {
                if (const auto* ps = m_sPs_manager->get(get_text_ps_id(m_parser, buffer)))
                {
                    type_mask = ps->type;
                    route_to_last_used = ps->route_to_last_used;
                }
            }
            else if (query_info.ps_packet)
            {
                if (const auto* ps = m_sPs_manager->get(stmt_id))
                {
                    type_mask = ps->type;
                    route_to_last_used = ps->route_to_last_used;
                    m_route_info.set_ps_continuation(query_continues_ps(buffer));
                }
            }
            else if (is_query && query_info.relates_to_previous)
            {
                route_to_last_used = true;
            }

            route_target = get_route_target(type_mask, trx_tracker);

            if (route_target == TARGET_SLAVE && route_to_last_used)
            {
                route_target = TARGET_LAST_USED;
            }
        }

        process_routing_hints(buffer.hints(), &route_target);

        if (trx_tracker.is_trx_ending() || Parser::type_mask_contains(type_mask, mxs::sql::TYPE_BEGIN_TRX))
        {
            // Transaction is ending or starting
            m_route_info.set_trx_still_read_only(true);
        }
        else if (trx_tracker.is_trx_active() && !query_type_is_read_only(type_mask))
        {
            // Transaction is no longer read-only
            m_route_info.set_trx_still_read_only(false);
        }
    }

    if (m_verbose && mxb_log_should_log(LOG_INFO))
    {
        log_transaction_status(buffer, type_mask, trx_tracker);
    }

    m_route_info.set_target(route_target);
    m_route_info.set_command(cmd);
    m_route_info.set_type_mask(type_mask);
    m_route_info.set_stmt_id(stmt_id);

    return m_route_info;
}

void QueryClassifier::commit_route_info_update(const GWBUF& buffer)
{
    if (m_route_info.multi_part_packet() || m_route_info.load_data_active())
    {
        return;
    }

    const auto type = m_route_info.type_mask();

    if (type & (mxs::sql::TYPE_PREPARE_NAMED_STMT | mxs::sql::TYPE_PREPARE_STMT))
    {
        mxb_assert(buffer.id() != 0 || Parser::type_mask_contains(type, mxs::sql::TYPE_PREPARE_NAMED_STMT));
        ps_store(buffer, buffer.id());
    }
    else if (type & mxs::sql::TYPE_DEALLOC_PREPARE)
    {
        ps_erase(buffer);
    }
    else if (type & mxs::sql::TYPE_CREATE_TMP_TABLE)
    {
        create_tmp_table(buffer, type);
    }
    else if (have_tmp_tables() && parser().get_operation(buffer) == mxs::sql::OP_DROP_TABLE)
    {
        foreach_table(*this, m_pSession, buffer, &QueryClassifier::delete_table);
    }
}

void QueryClassifier::update_from_reply(const mxs::Reply& reply)
{
    m_route_info.set_load_data_active(reply.state() == mxs::ReplyState::LOAD_DATA);

    if (reply.is_complete())
    {
        m_route_info.m_trx_tracker.fix_trx_state(reply);

        auto id = reply.generated_id();
        // The previous PS ID can be larger than the ID of the response being stored if multiple prepared
        // statements were sent at the same time.
        mxb_assert(m_prev_ps_id == id || id == 0);

        if (auto param_count = reply.param_count())
        {
            m_sPs_manager->set_param_count(id, param_count);
        }
    }
}

// static
bool QueryClassifier::find_table(QueryClassifier& qc, const std::string& table)
{
    if (qc.is_tmp_table(table))
    {
        MXB_INFO("Query targets a temporary table: %s", table.c_str());
        return false;
    }

    return true;
}

// static
bool QueryClassifier::delete_table(QueryClassifier& qc, const std::string& table)
{
    qc.remove_tmp_table(table);
    return true;
}

void QueryClassifier::revert_update()
{
    m_route_info = m_prev_route_info;
}
}
