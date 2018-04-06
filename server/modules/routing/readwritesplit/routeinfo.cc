/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "routeinfo.hh"
#include <maxscale/alloc.h>
#include <maxscale/queryclassifier.hh>
#include "rwsplitsession.hh"

#define RWSPLIT_TRACE_MSG_LEN 1000

using mxs::QueryClassifier;

namespace
{

/**
 * Examine the query type, transaction state and routing hints. Find out the
 * target for query routing.
 *
 *  @param qc                    The query classifier.
 *  @param command               The current command.
 *  @param qtype                 Type of query
 *  @param query_hints           Pointer to list of hints attached to the query buffer
 *
 *  @return bitfield including the routing target, or the target server name
 *          if the query would otherwise be routed to slave.
 */
route_target_t get_route_target(mxs::QueryClassifier& qc,
                                uint8_t command,
                                uint32_t qtype,
                                HINT *query_hints)
{
    int target = qc.get_route_target(command, qtype);

    /** Process routing hints */
    for (HINT* hint = query_hints; hint; hint = hint->next)
    {
        if (hint->type == HINT_ROUTE_TO_MASTER)
        {
            target = TARGET_MASTER; /*< override */
            MXS_DEBUG("Hint: route to master");
            break;
        }
        else if (hint->type == HINT_ROUTE_TO_NAMED_SERVER)
        {
            /**
             * Searching for a named server. If it can't be
             * found, the oroginal target is chosen.
             */
            target |= TARGET_NAMED_SERVER;
            MXS_DEBUG("Hint: route to named server: %s", (char*)hint->data);
        }
        else if (hint->type == HINT_ROUTE_TO_UPTODATE_SERVER)
        {
            /** not implemented */
            ss_dassert(false);
        }
        else if (hint->type == HINT_ROUTE_TO_ALL)
        {
            /** not implemented */
            ss_dassert(false);
        }
        else if (hint->type == HINT_PARAMETER)
        {
            if (strncasecmp((char*)hint->data, "max_slave_replication_lag",
                            strlen("max_slave_replication_lag")) == 0)
            {
                target |= TARGET_RLAG_MAX;
            }
            else
            {
                MXS_ERROR("Unknown hint parameter '%s' when "
                          "'max_slave_replication_lag' was expected.",
                          (char*)hint->data);
            }
        }
        else if (hint->type == HINT_ROUTE_TO_SLAVE)
        {
            target = TARGET_SLAVE;
            MXS_DEBUG("Hint: route to slave.");
        }
    }

    return (route_target_t)target;
}

/**
 * @brief Log the transaction status
 *
 * The router session and the query buffer are used to log the transaction
 * status, along with the query type (which is a generic description that
 * should be usable across all DB types).
 *
 * @param qc        The query classifier.
 * @param querybuf  Query buffer
 * @param qtype     Query type
 */
void
log_transaction_status(const QueryClassifier& qc, GWBUF *querybuf, uint32_t qtype)
{
    if (qc.large_query())
    {
        MXS_INFO("> Processing large request with more than 2^24 bytes of data");
    }
    else if (qc.load_data_state() == QueryClassifier::LOAD_DATA_INACTIVE)
    {
        uint8_t *packet = GWBUF_DATA(querybuf);
        unsigned char command = packet[4];
        int len = 0;
        char* sql;
        char *qtypestr = qc_typemask_to_string(qtype);
        if (!modutil_extract_SQL(querybuf, &sql, &len))
        {
            sql = (char*)"<non-SQL>";
        }

        if (len > RWSPLIT_TRACE_MSG_LEN)
        {
            len = RWSPLIT_TRACE_MSG_LEN;
        }

        MXS_SESSION *ses = qc.session();
        const char *autocommit = session_is_autocommit(ses) ? "[enabled]" : "[disabled]";
        const char *transaction = session_trx_is_active(ses) ? "[open]" : "[not open]";
        uint32_t plen = MYSQL_GET_PACKET_LEN(querybuf);
        const char *querytype = qtypestr == NULL ? "N/A" : qtypestr;
        const char *hint = querybuf->hint == NULL ? "" : ", Hint:";
        const char *hint_type = querybuf->hint == NULL ? "" : STRHINTTYPE(querybuf->hint->type);

        MXS_INFO("> Autocommit: %s, trx is %s, cmd: (0x%02x) %s, plen: %u, type: %s, stmt: %.*s%s %s",
                 autocommit, transaction, command, STRPACKETTYPE(command), plen,
                 querytype, len, sql, hint, hint_type);

        MXS_FREE(qtypestr);
    }
    else
    {
        MXS_INFO("> Processing LOAD DATA LOCAL INFILE: %lu bytes sent.",
                 qc.load_data_sent());
    }
}

/**
 * @brief Determine the type of a query
 *
 * @param querybuf      GWBUF containing the query
 * @param packet_type   Integer denoting DB specific enum
 * @param non_empty_packet  Boolean to be set by this function
 *
 * @return uint32_t the query type; also the non_empty_packet bool is set
 */
uint32_t determine_query_type(GWBUF *querybuf, int command)
{
    uint32_t type = QUERY_TYPE_UNKNOWN;

    switch (command)
    {
    case MXS_COM_QUIT: /*< 1 QUIT will close all sessions */
    case MXS_COM_INIT_DB: /*< 2 DDL must go to the master */
    case MXS_COM_REFRESH: /*< 7 - I guess this is session but not sure */
    case MXS_COM_DEBUG: /*< 0d all servers dump debug info to stdout */
    case MXS_COM_PING: /*< 0e all servers are pinged */
    case MXS_COM_CHANGE_USER: /*< 11 all servers change it accordingly */
    case MXS_COM_SET_OPTION: /*< 1b send options to all servers */
        type = QUERY_TYPE_SESSION_WRITE;
        break;

    case MXS_COM_CREATE_DB: /**< 5 DDL must go to the master */
    case MXS_COM_DROP_DB: /**< 6 DDL must go to the master */
    case MXS_COM_STMT_CLOSE: /*< free prepared statement */
    case MXS_COM_STMT_SEND_LONG_DATA: /*< send data to column */
    case MXS_COM_STMT_RESET: /*< resets the data of a prepared statement */
        type = QUERY_TYPE_WRITE;
        break;

    case MXS_COM_QUERY:
        type = qc_get_type_mask(querybuf);
        break;

    case MXS_COM_STMT_PREPARE:
        type = qc_get_type_mask(querybuf);
        type |= QUERY_TYPE_PREPARE_STMT;
        break;

    case MXS_COM_STMT_EXECUTE:
        /** Parsing is not needed for this type of packet */
        type = QUERY_TYPE_EXEC_STMT;
        break;

    case MXS_COM_SHUTDOWN: /**< 8 where should shutdown be routed ? */
    case MXS_COM_STATISTICS: /**< 9 ? */
    case MXS_COM_PROCESS_INFO: /**< 0a ? */
    case MXS_COM_CONNECT: /**< 0b ? */
    case MXS_COM_PROCESS_KILL: /**< 0c ? */
    case MXS_COM_TIME: /**< 0f should this be run in gateway ? */
    case MXS_COM_DELAYED_INSERT: /**< 10 ? */
    case MXS_COM_DAEMON: /**< 1d ? */
    default:
        break;
    }

    return type;
}

/**
 * If query is of type QUERY_TYPE_CREATE_TMP_TABLE then find out
 * the database and table name, create a hashvalue and
 * add it to the router client session's property. If property
 * doesn't exist then create it first.
 * @param qc The query classifier.
 * @param querybuf GWBUF containing the query
 * @param type The type of the query resolved so far
 */
void check_create_tmp_table(QueryClassifier& qc,  GWBUF *querybuf, uint32_t type)
{
    if (qc_query_is_type(type, QUERY_TYPE_CREATE_TMP_TABLE))
    {
        qc.set_have_tmp_tables(true);
        char* tblname = qc_get_created_table_name(querybuf);
        std::string table;

        if (tblname && *tblname && strchr(tblname, '.') == NULL)
        {
            const char* db = mxs_mysql_get_current_db(qc.session());
            table += db;
            table += ".";
            table += tblname;
        }

        /** Add the table to the set of temporary tables */
        qc.add_tmp_table(table);

        MXS_FREE(tblname);
    }
}

/**
 * Find callback for foreach_table
 */
bool find_table(QueryClassifier& qc, const std::string& table)
{
    if (qc.is_tmp_table(table))
    {
        MXS_INFO("Query targets a temporary table: %s", table.c_str());
        return false;
    }

    return true;
}

/**
 * @brief Map a function over the list of tables in the query
 *
 * @param qc       The query classifier.
 * @param querybuf The query to inspect
 * @param func     Callback that is called for each table
 *
 * @return True if all tables were iterated, false if the iteration was stopped early
 */
static bool foreach_table(QueryClassifier& qc, GWBUF* querybuf, bool (*func)(QueryClassifier& qc,
                                                                             const std::string&))
{
    bool rval = true;
    int n_tables;
    char** tables = qc_get_table_names(querybuf, &n_tables, true);

    for (int i = 0; i < n_tables; i++)
    {
        const char* db = mxs_mysql_get_current_db(qc.session());
        std::string table;

        if (strchr(tables[i], '.') == NULL)
        {
            table += db;
            table += ".";
        }

        table += tables[i];

        if (!func(qc, table))
        {
            rval = false;
            break;
        }
    }

    return rval;
}

/**
 * Check if the query targets a temporary table.
 * @param qc The query classifier.
 * @param querybuf GWBUF containing the query
 * @param type The type of the query resolved so far
 * @return The type of the query
 */
bool is_read_tmp_table(QueryClassifier& qc,
                       GWBUF *querybuf,
                       uint32_t qtype)
{
    bool rval = false;

    if (qc_query_is_type(qtype, QUERY_TYPE_READ) ||
        qc_query_is_type(qtype, QUERY_TYPE_LOCAL_READ) ||
        qc_query_is_type(qtype, QUERY_TYPE_USERVAR_READ) ||
        qc_query_is_type(qtype, QUERY_TYPE_SYSVAR_READ) ||
        qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_READ))
    {
        if (!foreach_table(qc, querybuf, find_table))
        {
            rval = true;
        }
    }

    return rval;
}

/**
 * Delete callback for foreach_table
 */
bool delete_table(QueryClassifier& qc, const std::string& table)
{
    qc.remove_tmp_table(table);
    return true;
}

/**
 * @brief Check for dropping of temporary tables
 *
 * Check if the query is a DROP TABLE... query and
 * if it targets a temporary table, remove it from the hashtable.
 * @param qc The query classifier
 * @param querybuf GWBUF containing the query
 * @param type The type of the query resolved so far
 */
void check_drop_tmp_table(QueryClassifier& qc, GWBUF *querybuf)
{
    if (qc_is_drop_table_query(querybuf))
    {
        foreach_table(qc, querybuf, delete_table);
    }
}

/**
 * @brief Determine if a packet contains a SQL query
 *
 * Packet type tells us this, but in a DB specific way. This function is
 * provided so that code that is not DB specific can find out whether a packet
 * contains a SQL query. Clearly, to be effective different functions must be
 * called for different DB types.
 *
 * @param packet_type   Type of packet (integer)
 * @return bool indicating whether packet contains a SQL query
 */
bool is_packet_a_query(int packet_type)
{
    return (packet_type == MXS_COM_QUERY);
}

bool check_for_sp_call(GWBUF *buf, uint8_t packet_type)
{
    return packet_type == MXS_COM_QUERY && qc_get_operation(buf) == QUERY_OP_CALL;
}

inline bool have_semicolon(const char* ptr, int len)
{
    for (int i = 0; i < len; i++)
    {
        if (ptr[i] == ';')
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Detect multi-statement queries
 *
 * It is possible that the session state is modified inside a multi-statement
 * query which would leave any slave sessions in an inconsistent state. Due to
 * this, for the duration of this session, all queries will be sent to the
 * master
 * if the current query contains a multi-statement query.
 * @param rses Router client session
 * @param buf Buffer containing the full query
 * @return True if the query contains multiple statements
 */
bool check_for_multi_stmt(const QueryClassifier& qc, GWBUF *buf, uint8_t packet_type)
{
    bool rval = false;

    if (qc.multi_statements_allowed() && packet_type == MXS_COM_QUERY)
    {
        char *ptr, *data = (char*)GWBUF_DATA(buf) + 5;
        /** Payload size without command byte */
        int buflen = gw_mysql_get_byte3((uint8_t *)GWBUF_DATA(buf)) - 1;

        if (have_semicolon(data, buflen) && (ptr = strnchr_esc_mysql(data, ';', buflen)))
        {
            /** Skip stored procedures etc. */
            while (ptr && is_mysql_sp_end(ptr, buflen - (ptr - data)))
            {
                ptr = strnchr_esc_mysql(ptr + 1, ';', buflen - (ptr - data) - 1);
            }

            if (ptr)
            {
                if (ptr < data + buflen &&
                    !is_mysql_statement_end(ptr, buflen - (ptr - data)))
                {
                    rval = true;
                }
            }
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
 * @param packet_type          Type of packet (database specific)
 * @param qtype                Query type
 *
 * @return QueryClassifier::CURRENT_TARGET_MASTER if the session should be fixed
 *         to the master, QueryClassifier::CURRENT_TARGET_UNDEFINED otherwise.
 */
QueryClassifier::current_target_t
handle_multi_temp_and_load(QueryClassifier& qc,
                           QueryClassifier::current_target_t current_target,
                           GWBUF *querybuf,
                           uint8_t packet_type,
                           uint32_t *qtype)
{
    QueryClassifier::current_target_t rv = QueryClassifier::CURRENT_TARGET_UNDEFINED;

    /** Check for multi-statement queries. If no master server is available
     * and a multi-statement is issued, an error is returned to the client
     * when the query is routed. */
    if ((current_target != QueryClassifier::CURRENT_TARGET_MASTER) &&
        (check_for_multi_stmt(qc, querybuf, packet_type) ||
         check_for_sp_call(querybuf, packet_type)))
    {
        MXS_INFO("Multi-statement query or stored procedure call, routing "
                 "all future queries to master.");
        rv = QueryClassifier::CURRENT_TARGET_MASTER;
    }

    /**
     * Check if the query has anything to do with temporary tables.
     */
    if (qc.have_tmp_tables() && is_packet_a_query(packet_type))
    {
        check_drop_tmp_table(qc, querybuf);
        if (is_read_tmp_table(qc, querybuf, *qtype))
        {
            *qtype |= QUERY_TYPE_MASTER_READ;
        }
    }

    check_create_tmp_table(qc, querybuf, *qtype);

    /**
     * Check if this is a LOAD DATA LOCAL INFILE query. If so, send all queries
     * to the master until the last, empty packet arrives.
     */
    if (qc.load_data_state() == QueryClassifier::LOAD_DATA_ACTIVE)
    {
        qc.append_load_data_sent(querybuf);
    }
    else if (is_packet_a_query(packet_type))
    {
        qc_query_op_t queryop = qc_get_operation(querybuf);
        if (queryop == QUERY_OP_LOAD)
        {
            qc.set_load_data_state(QueryClassifier::LOAD_DATA_START);
            qc.reset_load_data_sent();
        }
    }

    return rv;
}

/**
 * @brief Get the routing requirements for a query
 *
 * @param rses Router client session
 * @param buffer Buffer containing the query
 * @param command Output parameter where the packet command is stored
 * @param type    Output parameter where the query type is stored
 * @param stmt_id Output parameter where statement ID, if the query is a binary protocol command, is stored
 *
 * @return The target type where this query should be routed
 */
route_target_t get_target_type(RWSplitSession *rses, GWBUF *buffer,
                               uint8_t* command, uint32_t* type, uint32_t* stmt_id)
{
    route_target_t route_target = TARGET_MASTER;
    bool in_read_only_trx = rses->m_target_node && session_trx_is_read_only(rses->m_client->session);

    if (gwbuf_length(buffer) > MYSQL_HEADER_LEN)
    {
        *command = mxs_mysql_get_command(buffer);

        /**
         * If the session is inside a read-only transaction, we trust that the
         * server acts properly even when non-read-only queries are executed.
         * For this reason, we can skip the parsing of the statement completely.
         */
        if (in_read_only_trx)
        {
            *type = QUERY_TYPE_READ;
        }
        else
        {
            *type = determine_query_type(buffer, *command);

            QueryClassifier::current_target_t current_target;

            if (rses->m_target_node == NULL)
            {
                current_target = QueryClassifier::CURRENT_TARGET_UNDEFINED;
            }
            else if (rses->m_target_node == rses->m_current_master)
            {
                current_target = QueryClassifier::CURRENT_TARGET_MASTER;
            }
            else
            {
                current_target = QueryClassifier::CURRENT_TARGET_SLAVE;
            }

            current_target = handle_multi_temp_and_load(rses->qc(),
                                                        current_target,
                                                        buffer, *command, type);

            if (current_target == QueryClassifier::CURRENT_TARGET_MASTER)
            {
                /* If we do not have a master node, assigning the forced node is not
                 * effective since we don't have a node to force queries to. In this
                 * situation, assigning QUERY_TYPE_WRITE for the query will trigger
                 * the error processing. */
                if (rses->m_current_master && rses->m_current_master->in_use())
                {
                    rses->m_target_node = rses->m_current_master;
                }
                else
                {
                    *type |= QUERY_TYPE_WRITE;
                }
            }
        }

        if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            log_transaction_status(rses->qc(), buffer, *type);
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

        if (rses->m_target_node && rses->m_target_node == rses->m_current_master)
        {
            /** The session is locked to the master */
            route_target = TARGET_MASTER;

            if (qc_query_is_type(*type, QUERY_TYPE_PREPARE_NAMED_STMT) ||
                qc_query_is_type(*type, QUERY_TYPE_PREPARE_STMT))
            {
                gwbuf_set_type(buffer, GWBUF_TYPE_COLLECT_RESULT);
            }
        }
        else
        {
            if (!in_read_only_trx &&
                *command == MXS_COM_QUERY &&
                qc_get_operation(buffer) == QUERY_OP_EXECUTE)
            {
                std::string id = get_text_ps_id(buffer);
                *type = rses->qc().ps_get_type(id);
            }
            else if (mxs_mysql_is_ps_command(*command))
            {
                *stmt_id = rses->qc().ps_id_internal_get(buffer);
                *type = rses->qc().ps_get_type(*stmt_id);
            }

            route_target = get_route_target(rses->qc(), *command, *type, buffer->hint);
        }
    }
    else
    {
        /** Empty packet signals end of LOAD DATA LOCAL INFILE, send it to master*/
        rses->qc().set_load_data_state(QueryClassifier::LOAD_DATA_END);
        rses->qc().append_load_data_sent(buffer);
        MXS_INFO("> LOAD DATA LOCAL INFILE finished: %lu bytes sent.",
                 rses->qc().load_data_sent());
    }

    return route_target;
}

}

RouteInfo::RouteInfo(RWSplitSession* rses, GWBUF* buffer)
    : target(TARGET_UNDEFINED)
    , command(0xff)
    , type(QUERY_TYPE_UNKNOWN)
    , stmt_id(0)
{
    ss_dassert(rses);
    ss_dassert(rses->m_client);
    ss_dassert(rses->m_client->data);
    ss_dassert(buffer);

    target = get_target_type(rses, buffer, &command, &type, &stmt_id);
}
