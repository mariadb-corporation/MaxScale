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

#include "rwsplitsession.hh"
#include "rwsplit_internal.hh"
#include "routeinfo.hh"

#include <cmath>

using namespace maxscale;

RWSplitSession::RWSplitSession(RWSplit* instance, MXS_SESSION* session,
                               const SRWBackendList& backends,
                               const SRWBackend& master):
    rses_chk_top(CHK_NUM_ROUTER_SES),
    rses_closed(false),
    backends(backends),
    current_master(master),
    large_query(false),
    rses_config(instance->config()),
    rses_nbackends(instance->service()->n_dbref),
    load_data_state(LOAD_DATA_INACTIVE),
    have_tmp_tables(false),
    rses_load_data_sent(0),
    client_dcb(session->client_dcb),
    sescmd_count(1), // Needs to be a positive number to work
    expected_responses(0),
    query_queue(NULL),
    router(instance),
    sent_sescmd(0),
    recv_sescmd(0),
    gtid_pos(""),
    wait_gtid_state(EXPECTING_NOTHING),
    next_seq(0),
    rses_chk_tail(CHK_NUM_ROUTER_SES)
{
    if (rses_config.rw_max_slave_conn_percent)
    {
        int n_conn = 0;
        double pct = (double)rses_config.rw_max_slave_conn_percent / 100.0;
        n_conn = MXS_MAX(floor((double)rses_nbackends * pct), 1);
        rses_config.max_slave_connections = n_conn;
    }
}

RWSplitSession* RWSplitSession::create(RWSplit* router, MXS_SESSION* session)
{
    RWSplitSession* rses = NULL;

    if (router->have_enough_servers())
    {
        SRWBackendList backends = RWBackend::from_servers(router->service()->dbref);

        /**
         * At least the master must be found if the router is in the strict mode.
         * If sessions without master are allowed, only a slave must be found.
         */

        SRWBackend master;

        if (select_connect_backend_servers(router, session, backends, master,
                                           NULL, NULL, connection_type::ALL))
        {
            if ((rses = new RWSplitSession(router, session, backends, master)))
            {
                router->stats().n_sessions += 1;
            }
        }
    }

    return rses;
}

uint32_t get_internal_ps_id(RWSplitSession* rses, GWBUF* buffer)
{
    uint32_t rval = 0;

    // All COM_STMT type statements store the ID in the same place
    uint32_t id = mxs_mysql_extract_ps_id(buffer);
    ClientHandleMap::iterator it = rses->ps_handles.find(id);

    if (it != rses->ps_handles.end())
    {
        rval = it->second;
    }
    else
    {
        MXS_WARNING("Client requests unknown prepared statement ID '%u' that "
                    "does not map to an internal ID", id);
    }

    return rval;
}
