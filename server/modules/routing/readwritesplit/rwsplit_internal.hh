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

#include <maxscale/cppdefs.hh>

#include <string>

#include <maxscale/query_classifier.h>
#include <maxscale/protocol/mysql.h>

#include "readwritesplit.hh"

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
bool route_single_stmt(ROUTER_INSTANCE *inst, ROUTER_CLIENT_SES *rses,
                       GWBUF *querybuf);
void closed_session_reply(GWBUF *querybuf);
void print_error_packet(ROUTER_CLIENT_SES *rses, GWBUF *buf, DCB *dcb);
void check_session_command_reply(GWBUF *writebuf, SRWBackend bref);
bool execute_sescmd_in_backend(SRWBackend& backend_ref);
bool handle_target_is_all(route_target_t route_target,
                          ROUTER_INSTANCE *inst, ROUTER_CLIENT_SES *rses,
                          GWBUF *querybuf, int packet_type, uint32_t qtype);
uint8_t determine_packet_type(GWBUF *querybuf, bool *non_empty_packet);
void log_transaction_status(ROUTER_CLIENT_SES *rses, GWBUF *querybuf, uint32_t qtype);
bool is_packet_a_query(int packet_type);
bool send_readonly_error(DCB *dcb);

/*
 * The following are implemented in readwritesplit.c
 */
int router_handle_state_switch(DCB *dcb, DCB_REASON reason, void *data);
SRWBackend get_backend_from_dcb(ROUTER_CLIENT_SES *rses, DCB *dcb);
int rses_get_max_slavecount(ROUTER_CLIENT_SES *rses);
int rses_get_max_replication_lag(ROUTER_CLIENT_SES *rses);

/*
 * The following are implemented in rwsplit_route_stmt.c
 */

bool route_single_stmt(ROUTER_INSTANCE *inst, ROUTER_CLIENT_SES *rses,
                       GWBUF *querybuf);
SRWBackend get_target_backend(ROUTER_CLIENT_SES *rses, backend_type_t btype,
                              char *name, int max_rlag);
route_target_t get_route_target(ROUTER_CLIENT_SES *rses, uint8_t command,
                                uint32_t qtype, HINT *hint);
void handle_multi_temp_and_load(ROUTER_CLIENT_SES *rses, GWBUF *querybuf,
                                uint8_t packet_type, uint32_t *qtype);
SRWBackend handle_hinted_target(ROUTER_CLIENT_SES *rses, GWBUF *querybuf,
                                route_target_t route_target);
SRWBackend handle_slave_is_target(ROUTER_INSTANCE *inst, ROUTER_CLIENT_SES *rses,
                                  uint8_t cmd, uint32_t id);
bool handle_master_is_target(ROUTER_INSTANCE *inst, ROUTER_CLIENT_SES *rses,
                             SRWBackend* dest);
bool handle_got_target(ROUTER_INSTANCE *inst, ROUTER_CLIENT_SES *rses,
                       GWBUF *querybuf, SRWBackend& target, bool store);
bool route_session_write(ROUTER_CLIENT_SES *rses, GWBUF *querybuf,
                         uint8_t command, uint32_t type);

void process_sescmd_response(ROUTER_CLIENT_SES* rses, SRWBackend& bref,
                             GWBUF** ppPacket, bool* reconnect);
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
                                    select_criteria_t select_criteria,
                                    MXS_SESSION *session,
                                    ROUTER_INSTANCE *router,
                                    ROUTER_CLIENT_SES *rses,
                                    connection_type type);

/*
 * The following are implemented in rwsplit_tmp_table_multi.c
 */
void check_drop_tmp_table(ROUTER_CLIENT_SES *router_cli_ses, GWBUF *querybuf);
bool is_read_tmp_table(ROUTER_CLIENT_SES *router_cli_ses,
                       GWBUF *querybuf,
                       uint32_t type);
void check_create_tmp_table(ROUTER_CLIENT_SES *router_cli_ses,
                            GWBUF *querybuf, uint32_t type);
bool check_for_multi_stmt(GWBUF *buf, void *protocol, uint8_t packet_type);
uint32_t determine_query_type(GWBUF *querybuf, int packet_type, bool non_empty_packet);

void close_all_connections(ROUTER_CLIENT_SES* rses);

/**
 * @brief Extract text identifier of a PREPARE or EXECUTE statement
 *
 * @param buffer Buffer containing a PREPARE or EXECUTE command
 *
 * @return The string identifier of the statement
 */
std::string extract_text_ps_id(GWBUF* buffer);
