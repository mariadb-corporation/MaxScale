#pragma once
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

#include "readwritesplit.hh"

#include <string>
#include <utility>

#include <maxscale/query_classifier.h>
#include <maxscale/protocol/mysql.h>

#include "rwsplitsession.hh"

class RouteInfo;

#define RW_CHK_DCB(b, d) \
do{ \
    if(d->state == DCB_STATE_DISCONNECTED){ \
        MXS_NOTICE("DCB was closed on line %d and another attempt to close it is  made on line %d." , \
            (b) ? (b)->closed_at : -1, __LINE__); \
        } \
}while (false)

#define RW_CLOSE_BREF(b) do{ if (b){ (b)->closed_at = __LINE__; } } while (false)

/*
 * The following are implemented in rwsplit_mysql.c
 */
bool route_single_stmt(RWSplit *inst, RWSplitSession *rses,
                       GWBUF *querybuf);
void closed_session_reply(GWBUF *querybuf);
void print_error_packet(RWSplitSession *rses, GWBUF *buf, DCB *dcb);
void check_session_command_reply(GWBUF *buffer, mxs::SRWBackend& backend);
bool execute_sescmd_in_backend(mxs::SRWBackend& backend_ref);
bool handle_target_is_all(route_target_t route_target,
                          RWSplit *inst, RWSplitSession *rses,
                          GWBUF *querybuf, int packet_type, uint32_t qtype);
bool send_readonly_error(DCB *dcb);

/*
 * The following are implemented in readwritesplit.c
 */
int router_handle_state_switch(DCB *dcb, DCB_REASON reason, void *data);
int rses_get_max_replication_lag(RWSplitSession *rses);

/*
 * The following are implemented in rwsplit_route_stmt.c
 */

bool route_single_stmt(RWSplit *inst, RWSplitSession *rses,
                       GWBUF *querybuf, const RouteInfo& info);
mxs::SRWBackend get_target_backend(RWSplitSession *rses, backend_type_t btype,
                              char *name, int max_rlag);
mxs::SRWBackend handle_hinted_target(RWSplitSession *rses, GWBUF *querybuf,
                                route_target_t route_target);
mxs::SRWBackend handle_slave_is_target(RWSplit *inst, RWSplitSession *rses,
                                  uint8_t cmd, uint32_t id);
bool handle_master_is_target(RWSplit *inst, RWSplitSession *rses,
                             mxs::SRWBackend* dest);
bool handle_got_target(RWSplit *inst, RWSplitSession *rses,
                       GWBUF *querybuf, mxs::SRWBackend& target, bool store);
bool route_session_write(RWSplitSession *rses, GWBUF *querybuf,
                         uint8_t command, uint32_t type);

void process_sescmd_response(RWSplitSession* rses, mxs::SRWBackend& bref, GWBUF** ppPacket);
/*
 * The following are implemented in rwsplit_select_backends.c
 */

/** What sort of connections should be create */
enum connection_type
{
    ALL,
    SLAVE
};

bool select_connect_backend_servers(RWSplit *inst, MXS_SESSION *session,
                                    mxs::SRWBackendList& backends,
                                    mxs::SRWBackend& current_master,
                                    mxs::SessionCommandList* sescmd,
                                    int* expected_responses,
                                    connection_type type);
mxs::SRWBackend get_root_master(const mxs::SRWBackendList& backends);

/**
 * Get total slave count and connected slave count
 *
 * @param backends List of backend servers
 * @param master   Current master
 *
 * @return Total number of slaves and number of slaves we are connected to
 */
std::pair<int, int> get_slave_counts(mxs::SRWBackendList& backends, mxs::SRWBackend& master);

/*
 * The following are implemented in rwsplit_tmp_table_multi.c
 */
void close_all_connections(mxs::SRWBackendList& backends);
