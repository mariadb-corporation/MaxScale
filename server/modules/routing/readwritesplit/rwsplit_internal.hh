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

static inline bool is_ps_command(uint8_t cmd)
{
    return cmd == MXS_COM_STMT_EXECUTE ||
           cmd == MXS_COM_STMT_SEND_LONG_DATA ||
           cmd == MXS_COM_STMT_CLOSE ||
           cmd == MXS_COM_STMT_FETCH ||
           cmd == MXS_COM_STMT_RESET;
}

/*
 * The following are implemented in rwsplit_mysql.c
 */
bool route_single_stmt(RWSplit *inst, RWSplitSession *rses,
                       GWBUF *querybuf);
void closed_session_reply(GWBUF *querybuf);
void print_error_packet(RWSplitSession *rses, GWBUF *buf, DCB *dcb);
void check_session_command_reply(GWBUF *buffer, SRWBackend& backend);
bool execute_sescmd_in_backend(SRWBackend& backend_ref);
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
SRWBackend get_target_backend(RWSplitSession *rses, backend_type_t btype,
                              char *name, int max_rlag);
SRWBackend handle_hinted_target(RWSplitSession *rses, GWBUF *querybuf,
                                route_target_t route_target);
SRWBackend handle_slave_is_target(RWSplit *inst, RWSplitSession *rses,
                                  uint8_t cmd, uint32_t id);
bool handle_master_is_target(RWSplit *inst, RWSplitSession *rses,
                             SRWBackend* dest);
bool handle_got_target(RWSplit *inst, RWSplitSession *rses,
                       GWBUF *querybuf, SRWBackend& target, bool store);
bool route_session_write(RWSplitSession *rses, GWBUF *querybuf,
                         uint8_t command, uint32_t type);

void process_sescmd_response(RWSplitSession* rses, SRWBackend& bref, GWBUF** ppPacket);
/*
 * The following are implemented in rwsplit_select_backends.c
 */

/** What sort of connections should be create */
enum connection_type
{
    ALL,
    SLAVE
};

bool select_connect_backend_servers(int router_nservers,
                                    int max_nslaves,
                                    MXS_SESSION *session,
                                    const Config& config,
                                    SRWBackendList& backends,
                                    SRWBackend& current_master,
                                    mxs::SessionCommandList* sescmd,
                                    int* expected_responses,
                                    connection_type type);
/*
 * The following are implemented in rwsplit_tmp_table_multi.c
 */
void close_all_connections(SRWBackendList& backends);
