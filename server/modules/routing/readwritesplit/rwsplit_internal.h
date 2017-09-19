#pragma once
#ifndef _RWSPLIT_INTERNAL_H
#define _RWSPLIT_INTERNAL_H
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
 * File:   rwsplit_internal.h
 * Author: mbrampton
 *
 * Created on 08 August 2016, 11:54
 */

#include <maxscale/cdefs.h>
#include <maxscale/query_classifier.h>

MXS_BEGIN_DECLS

/* This needs to be removed along with dependency on it - see the
 * rwsplit_tmp_table_multi functions
 */
#include <maxscale/protocol/mysql.h>

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
void live_session_reply(GWBUF **querybuf, ROUTER_CLIENT_SES *rses);
void print_error_packet(ROUTER_CLIENT_SES *rses, GWBUF *buf, DCB *dcb);
void check_session_command_reply(GWBUF *writebuf, sescmd_cursor_t *scur, backend_ref_t *bref);
bool execute_sescmd_in_backend(backend_ref_t *backend_ref);
bool handle_target_is_all(route_target_t route_target,
                          ROUTER_INSTANCE *inst, ROUTER_CLIENT_SES *rses,
                          GWBUF *querybuf, int packet_type, qc_query_type_t qtype);
int determine_packet_type(GWBUF *querybuf, bool *non_empty_packet);
void log_transaction_status(ROUTER_CLIENT_SES *rses, GWBUF *querybuf, qc_query_type_t qtype);
bool is_packet_a_one_way_message(int packet_type);
sescmd_cursor_t *backend_ref_get_sescmd_cursor(backend_ref_t *bref);
bool is_packet_a_query(int packet_type);
bool send_readonly_error(DCB *dcb);

/*
 * The following are implemented in readwritesplit.c
 */
void bref_clear_state(backend_ref_t *bref, bref_state_t state);
void bref_set_state(backend_ref_t *bref, bref_state_t state);
int router_handle_state_switch(DCB *dcb, DCB_REASON reason, void *data);
backend_ref_t *get_bref_from_dcb(ROUTER_CLIENT_SES *rses, DCB *dcb);
void rses_property_done(rses_property_t *prop);
int rses_get_max_slavecount(ROUTER_CLIENT_SES *rses, int router_nservers);
int rses_get_max_replication_lag(ROUTER_CLIENT_SES *rses);

/*
 * The following are implemented in rwsplit_route_stmt.c
 */

bool route_single_stmt(ROUTER_INSTANCE *inst, ROUTER_CLIENT_SES *rses,
                       GWBUF *querybuf);
int rwsplit_hashkeyfun(const void *key);
int rwsplit_hashcmpfun(const void *v1, const void *v2);
void *rwsplit_hstrdup(const void *fval);
void rwsplit_hfree(void *fval);
bool rwsplit_get_dcb(DCB **p_dcb, ROUTER_CLIENT_SES *rses, backend_type_t btype,
                     char *name, int max_rlag);
route_target_t get_route_target(ROUTER_CLIENT_SES *rses,
                                qc_query_type_t qtype, HINT *hint);
rses_property_t *rses_property_init(rses_property_type_t prop_type);
int rses_property_add(ROUTER_CLIENT_SES *rses, rses_property_t *prop);
void handle_multi_temp_and_load(ROUTER_CLIENT_SES *rses, GWBUF *querybuf,
                                int packet_type, int *qtype);
bool handle_hinted_target(ROUTER_CLIENT_SES *rses, GWBUF *querybuf,
                          route_target_t route_target, DCB **target_dcb);
bool handle_slave_is_target(ROUTER_INSTANCE *inst, ROUTER_CLIENT_SES *rses,
                            DCB **target_dcb);
bool handle_master_is_target(ROUTER_INSTANCE *inst, ROUTER_CLIENT_SES *rses,
                             DCB **target_dcb);
bool handle_got_target(ROUTER_INSTANCE *inst, ROUTER_CLIENT_SES *rses,
                       GWBUF *querybuf, DCB *target_dcb, bool store);
bool route_session_write(ROUTER_CLIENT_SES *router_cli_ses,
                         GWBUF *querybuf, ROUTER_INSTANCE *inst,
                         int packet_type,
                         qc_query_type_t qtype);

/*
 * The following are implemented in rwsplit_session_cmd.c
*/
mysql_sescmd_t *rses_property_get_sescmd(rses_property_t *prop);
mysql_sescmd_t *mysql_sescmd_init(rses_property_t *rses_prop,
                                  GWBUF *sescmd_buf,
                                  unsigned char packet_type,
                                  ROUTER_CLIENT_SES *rses);
void mysql_sescmd_done(mysql_sescmd_t *sescmd);
mysql_sescmd_t *sescmd_cursor_get_command(sescmd_cursor_t *scur);
bool sescmd_cursor_is_active(sescmd_cursor_t *sescmd_cursor);
void sescmd_cursor_set_active(sescmd_cursor_t *sescmd_cursor,
                              bool value);
bool execute_sescmd_history(backend_ref_t *bref);
GWBUF *sescmd_cursor_clone_querybuf(sescmd_cursor_t *scur);
GWBUF *sescmd_cursor_process_replies(GWBUF *replybuf,
                                     backend_ref_t *bref,
                                     bool *reconnect);

/*
 * The following are implemented in rwsplit_select_backends.c
 */
bool select_connect_backend_servers(backend_ref_t **p_master_ref,
                                    backend_ref_t *backend_ref,
                                    int router_nservers, int max_nslaves,
                                    int max_slave_rlag,
                                    select_criteria_t select_criteria,
                                    MXS_SESSION *session,
                                    ROUTER_INSTANCE *router,
                                    bool active_session);

/*
 * The following are implemented in rwsplit_tmp_table_multi.c
 */
void check_drop_tmp_table(ROUTER_CLIENT_SES *router_cli_ses,
                          GWBUF *querybuf,
                          mysql_server_cmd_t packet_type);
bool is_read_tmp_table(ROUTER_CLIENT_SES *router_cli_ses,
                       GWBUF *querybuf,
                       qc_query_type_t type);
void check_create_tmp_table(ROUTER_CLIENT_SES *router_cli_ses,
                            GWBUF *querybuf, qc_query_type_t type);
bool check_for_multi_stmt(GWBUF *buf, void *protocol, mysql_server_cmd_t packet_type);
bool check_for_sp_call(GWBUF *buf, mysql_server_cmd_t packet_type);
qc_query_type_t determine_query_type(GWBUF *querybuf, int packet_type, bool non_empty_packet);
void close_failed_bref(backend_ref_t *bref, bool fatal);

#ifdef __cplusplus
}
#endif

MXS_END_DECLS

#endif /* RWSPLIT_INTERNAL_H */

