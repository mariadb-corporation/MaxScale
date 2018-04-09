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
        (qc.check_for_multi_stmt(querybuf, packet_type) ||
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
        qc.check_drop_tmp_table(querybuf);
        if (qc.is_read_tmp_table(querybuf, *qtype))
        {
            *qtype |= QUERY_TYPE_MASTER_READ;
        }
    }

    qc.check_create_tmp_table(querybuf, *qtype);

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
 * @param qc      The query classifier.
 * @param current_target
 * @param buffer  Buffer containing the query
 * @param command Output parameter where the packet command is stored
 * @param type    Output parameter where the query type is stored
 * @param stmt_id Output parameter where statement ID, if the query is a binary protocol command, is stored
 *
 * @return The target type where this query should be routed
 */
route_target_t get_target_type(QueryClassifier& qc,
                               QueryClassifier::current_target_t current_target,
                               GWBUF *buffer,
                               uint8_t* command,
                               uint32_t* type,
                               uint32_t* stmt_id)
{
    route_target_t route_target = TARGET_MASTER;

    // TODO: It may be sufficient to simply check whether we are in a read-only
    // TODO: transaction.
    bool in_read_only_trx =
        (current_target != QueryClassifier::CURRENT_TARGET_UNDEFINED) &&
        session_trx_is_read_only(qc.session());

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
            *type = QueryClassifier::determine_query_type(buffer, *command);

            current_target = handle_multi_temp_and_load(qc,
                                                        current_target,
                                                        buffer, *command, type);

            if (current_target == QueryClassifier::CURRENT_TARGET_MASTER)
            {
                /* If we do not have a master node, assigning the forced node is not
                 * effective since we don't have a node to force queries to. In this
                 * situation, assigning QUERY_TYPE_WRITE for the query will trigger
                 * the error processing. */
                if (!qc.handler()->lock_to_master())
                {
                    *type |= QUERY_TYPE_WRITE;
                }
            }
        }

        if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            qc.log_transaction_status(buffer, *type);
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

        if (qc.handler()->is_locked_to_master())
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
                *type = qc.ps_get_type(id);
            }
            else if (mxs_mysql_is_ps_command(*command))
            {
                *stmt_id = qc.ps_id_internal_get(buffer);
                *type = qc.ps_get_type(*stmt_id);
            }

            route_target = static_cast<route_target_t>(qc.get_route_target(*command, *type, buffer->hint));
        }
    }
    else
    {
        /** Empty packet signals end of LOAD DATA LOCAL INFILE, send it to master*/
        qc.set_load_data_state(QueryClassifier::LOAD_DATA_END);
        qc.append_load_data_sent(buffer);
        MXS_INFO("> LOAD DATA LOCAL INFILE finished: %lu bytes sent.",
                 qc.load_data_sent());
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

    target = get_target_type(rses->qc(), current_target, buffer, &command, &type, &stmt_id);
}
