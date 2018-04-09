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

            current_target = qc.handle_multi_temp_and_load(current_target,
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
