#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cdefs.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <maxscale/log_manager.h>

MXS_BEGIN_DECLS

#if defined(SS_DEBUG)
#include <maxscale/log_manager.h>
# define ss_dassert(exp) do { if(!(exp)){\
        const char *debug_expr = #exp;  /** The MXS_ERROR marco doesn't seem to like stringification */ \
        MXS_ERROR("debug assert at %s:%d failed: %s\n", (char*)__FILE__, __LINE__, debug_expr); \
        fprintf(stderr, "debug assert at %s:%d failed: %s\n", (char*)__FILE__, __LINE__, debug_expr); \
        raise(SIGABRT);} } while (false)
#define ss_info_dassert(exp,info) do { if(!(exp)){\
        const char *debug_expr = #exp; \
        MXS_ERROR("debug assert at %s:%d failed: %s (%s)\n", (char*)__FILE__, __LINE__, info, debug_expr); \
        fprintf(stderr, "debug assert at %s:%d failed: %s (%s)\n", (char*)__FILE__, __LINE__, info, debug_expr); \
        raise(SIGABRT);} } while (false)
# define ss_debug(exp) exp
# define ss_dfprintf fprintf
# define ss_dfflush  fflush
# define ss_dfwrite  fwrite
#else /* SS_DEBUG */

# define ss_debug(exp)
# define ss_dfprintf(a, b, ...)
# define ss_dfflush(s)
# define ss_dfwrite(a, b, c, d)
# define ss_dassert(exp)
# define ss_info_dassert(exp, info)

#endif /* SS_DEBUG */

#define CHK_NUM_BASE 101

# define STRBOOL(b) ((b) ? "true" : "false")

# define STRQTYPE(t)    ((t) == QUERY_TYPE_WRITE ? "QUERY_TYPE_WRITE" : \
                         ((t) == QUERY_TYPE_READ ? "QUERY_TYPE_READ" :  \
                          ((t) == QUERY_TYPE_SESSION_WRITE ? "QUERY_TYPE_SESSION_WRITE" : \
                           ((t) == QUERY_TYPE_UNKNOWN ? "QUERY_TYPE_UNKNOWN" : \
                            ((t) == QUERY_TYPE_LOCAL_READ ? "QUERY_TYPE_LOCAL_READ" : \
                             ((t) == QUERY_TYPE_MASTER_READ ? "QUERY_TYPE_MASTER_READ" : \
                              ((t) == QUERY_TYPE_USERVAR_WRITE ? "QUERY_TYPE_USERVAR_WRITE" : \
                               ((t) == QUERY_TYPE_USERVAR_READ ? "QUERY_TYPE_USERVAR_READ" : \
                                ((t) == QUERY_TYPE_SYSVAR_READ ? "QUERY_TYPE_SYSVAR_READ" : \
                                 ((t) == QUERY_TYPE_GSYSVAR_READ ? "QUERY_TYPE_GSYSVAR_READ" : \
                                  ((t) == QUERY_TYPE_GSYSVAR_WRITE ? "QUERY_TYPE_GSYSVAR_WRITE" : \
                                   ((t) == QUERY_TYPE_BEGIN_TRX ? "QUERY_TYPE_BEGIN_TRX" : \
                                    ((t) == QUERY_TYPE_ENABLE_AUTOCOMMIT ? "QUERY_TYPE_ENABLE_AUTOCOMMIT" : \
                                     ((t) == QUERY_TYPE_DISABLE_AUTOCOMMIT ? "QUERY_TYPE_DISABLE_AUTOCOMMIT" : \
                                      ((t) == QUERY_TYPE_ROLLBACK ? "QUERY_TYPE_ROLLBACK" : \
                                       ((t) == QUERY_TYPE_COMMIT ? "QUERY_TYPE_COMMIT" : \
                                        ((t) == QUERY_TYPE_PREPARE_NAMED_STMT ? "QUERY_TYPE_PREPARE_NAMED_STMT" : \
                                         ((t) == QUERY_TYPE_PREPARE_STMT ? "QUERY_TYPE_PREPARE_STMT" : \
                                          ((t) == QUERY_TYPE_EXEC_STMT ? "QUERY_TYPE_EXEC_STMT" : \
                                           ((t) == QUERY_TYPE_CREATE_TMP_TABLE ? "QUERY_TYPE_CREATE_TMP_TABLE" : \
                                            ((t) == QUERY_TYPE_READ_TMP_TABLE ? "QUERY_TYPE_READ_TMP_TABLE" : \
                                             ((t) == QUERY_TYPE_SHOW_DATABASES ? "QUERY_TYPE_SHOW_DATABASES" : \
                                              ((t) == QUERY_TYPE_SHOW_TABLES ? "QUERY_TYPE_SHOW_TABLES" : \
                                               "Unknown query type")))))))))))))))))))))))

#define STRLOGPRIORITYNAME(n)                   \
    ((n) == LOG_EMERG ? "LOG_EMERG" :           \
     ((n) == LOG_ALERT ? "LOG_ALERT" :          \
      ((n) == LOG_CRIT ? "LOG_CRIT" :           \
       ((n) == LOG_ERR ? "LOG_ERR" :            \
        ((n) == LOG_WARNING ? "LOG_WARNING" :   \
         ((n) == LOG_NOTICE ? "LOG_NOTICE" :    \
          ((n) == LOG_INFO ? "LOG_INFO" :       \
           ((n) == LOG_DEBUG ? "LOG_DEBUG" :    \
            "Unknown log priority"))))))))

#define STRDCBSTATE(s) ((s) == DCB_STATE_ALLOC ? "DCB_STATE_ALLOC" :    \
                        ((s) == DCB_STATE_POLLING ? "DCB_STATE_POLLING" : \
                         ((s) == DCB_STATE_LISTENING ? "DCB_STATE_LISTENING" : \
                          ((s) == DCB_STATE_DISCONNECTED ? "DCB_STATE_DISCONNECTED" : \
                           ((s) == DCB_STATE_NOPOLLING ? "DCB_STATE_NOPOLLING" : \
                             ((s) == DCB_STATE_UNDEFINED ? "DCB_STATE_UNDEFINED" : "DCB_STATE_UNKNOWN"))))))

#define STRSESSIONSTATE(s) ((s) == SESSION_STATE_ALLOC ? "SESSION_STATE_ALLOC" : \
                            ((s) == SESSION_STATE_DUMMY ? "SESSION_STATE_DUMMY" : \
                             ((s) == SESSION_STATE_READY ? "SESSION_STATE_READY" : \
                              ((s) == SESSION_STATE_LISTENER ? "SESSION_STATE_LISTENER" : \
                               ((s) == SESSION_STATE_ROUTER_READY ? "SESSION_STATE_ROUTER_READY" : \
                                ((s) == SESSION_STATE_LISTENER_STOPPED ? "SESSION_STATE_LISTENER_STOPPED" : \
                                 (s) == SESSION_STATE_STOPPING ? "SESSION_STATE_STOPPING": \
                                 "SESSION_STATE_UNKNOWN"))))))

#define STRPROTOCOLSTATE(s) ((s) == MXS_AUTH_STATE_INIT ? "MXS_AUTH_STATE_INIT" : \
                             ((s) == MXS_AUTH_STATE_PENDING_CONNECT ? "MXS_AUTH_STATE_PENDING_CONNECT" : \
                              ((s) == MXS_AUTH_STATE_CONNECTED ? "MXS_AUTH_STATE_CONNECTED" : \
                               ((s) == MXS_AUTH_STATE_MESSAGE_READ ? "MXS_AUTH_STATE_MESSAGE_READ" : \
                                ((s) == MXS_AUTH_STATE_RESPONSE_SENT ? "MXS_AUTH_STATE_RESPONSE_SENT" : \
                                 ((s) == MXS_AUTH_STATE_FAILED ? "MXS_AUTH_STATE_FAILED" : \
                                  ((s) == MXS_AUTH_STATE_COMPLETE ? "MXS_AUTH_STATE_COMPLETE" : \
                                   "UNKNOWN AUTH STATE")))))))

#define STRITEMTYPE(t) ((t) == Item::FIELD_ITEM ? "FIELD_ITEM" :        \
                        ((t) == Item::FUNC_ITEM ? "FUNC_ITEM" :         \
                         ((t) == Item::SUM_FUNC_ITEM ? "SUM_FUNC_ITEM" : \
                          ((t) == Item::STRING_ITEM ? "STRING_ITEM" :   \
                           ((t) == Item::INT_ITEM ? "INT_ITEM" :        \
                            ((t) == Item::REAL_ITEM ? "REAL_ITEM" :     \
                             ((t) == Item::NULL_ITEM ? "NULL_ITEM" :    \
                              ((t) == Item::VARBIN_ITEM ? "VARBIN_ITEM" : \
                               ((t) == Item::COPY_STR_ITEM ? "COPY_STR_ITEM" : \
                                ((t) == Item::FIELD_AVG_ITEM ? "FIELD_AVG_ITEM" : \
                                 ((t) == Item::DEFAULT_VALUE_ITEM ? "DEFAULT_VALUE_ITEM" : \
                                  ((t) == Item::PROC_ITEM ? "PROC_ITEM" : \
                                   ((t) == Item::COND_ITEM ? "COND_ITEM" : \
                                    ((t) == Item::REF_ITEM ? "REF_ITEM" : \
                                     (t) == Item::FIELD_STD_ITEM ? "FIELD_STD_ITEM" : \
                                     ((t) == Item::FIELD_VARIANCE_ITEM ? "FIELD_VARIANCE_ITEM" : \
                                      ((t) == Item::INSERT_VALUE_ITEM ? "INSERT_VALUE_ITEM": \
                                       ((t) == Item::SUBSELECT_ITEM ? "SUBSELECT_ITEM" : \
                                        ((t) == Item::ROW_ITEM ? "ROW_ITEM" : \
                                         ((t) == Item::CACHE_ITEM ? "CACHE_ITEM" : \
                                          ((t) == Item::TYPE_HOLDER ? "TYPE_HOLDER" : \
                                           ((t) == Item::PARAM_ITEM ? "PARAM_ITEM" : \
                                            ((t) == Item::TRIGGER_FIELD_ITEM ? "TRIGGER_FIELD_ITEM" : \
                                             ((t) == Item::DECIMAL_ITEM ? "DECIMAL_ITEM" : \
                                              ((t) == Item::XPATH_NODESET ? "XPATH_NODESET" : \
                                               ((t) == Item::XPATH_NODESET_CMP ? "XPATH_NODESET_CMP" : \
                                                ((t) == Item::VIEW_FIXER_ITEM ? "VIEW_FIXER_ITEM" : \
                                                 ((t) == Item::EXPR_CACHE_ITEM ? "EXPR_CACHE_ITEM" : \
                                                  "Unknown item")))))))))))))))))))))))))))

#define STRDCBROLE(r) ((r) == DCB_ROLE_SERVICE_LISTENER ? "DCB_ROLE_SERVICE_LISTENER" : \
                       ((r) == DCB_ROLE_CLIENT_HANDLER ? "DCB_ROLE_CLIENT_HANDLER" : \
                        ((r) == DCB_ROLE_BACKEND_HANDLER ? "DCB_ROLE_BACKEND_HANDLER" : \
                         ((r) == DCB_ROLE_INTERNAL ? "DCB_ROLE_INTERNAL" : \
                          "UNKNOWN DCB ROLE"))))

#define STRBETYPE(t) ((t) == BE_MASTER ? "BE_MASTER" :          \
                      ((t) == BE_SLAVE ? "BE_SLAVE" :           \
                       ((t) == BE_UNDEFINED ? "BE_UNDEFINED" :  \
                        "Unknown backend tpe")))

#define STRCRITERIA(c) ((c) == UNDEFINED_CRITERIA ? "UNDEFINED_CRITERIA" : \
                        ((c) == LEAST_GLOBAL_CONNECTIONS ? "LEAST_GLOBAL_CONNECTIONS" : \
                         ((c) == LEAST_ROUTER_CONNECTIONS ? "LEAST_ROUTER_CONNECTIONS" : \
                          ((c) == LEAST_BEHIND_MASTER ? "LEAST_BEHIND_MASTER"           : \
                           ((c) == LEAST_CURRENT_OPERATIONS ? "LEAST_CURRENT_OPERATIONS" : "Unknown criteria")))))

#define STRSRVSTATUS(s) (server_is_master(s)  ? "RUNNING MASTER" :      \
                         (server_is_slave(s)   ? "RUNNING SLAVE" :      \
                          (server_is_joined(s)  ? "RUNNING JOINED" :    \
                           (server_is_ndb(s)     ? "RUNNING NDB" :      \
                            ((server_is_running(s) && server_is_in_maint(s)) ? "RUNNING MAINTENANCE" : \
                             (server_is_relay(s) ? "RUNNING RELAY" : \
                              (server_is_usable(s) ? "RUNNING (only)" : \
                               (server_is_down(s) ? "DOWN" : "UNKNOWN STATUS"))))))))

#define STRTARGET(t)    (t == TARGET_ALL ? "TARGET_ALL" :               \
                         (t == TARGET_MASTER ? "TARGET_MASTER" :        \
                          (t == TARGET_SLAVE ? "TARGET_SLAVE" :         \
                           (t == TARGET_NAMED_SERVER ? "TARGET_NAMED_SERVER" : \
                            (t == TARGET_UNDEFINED ? "TARGET_UNDEFINED" : \
                             "Unknown target value")))))

#define BREFSRV(b)  (b->bref_backend->backend_server)


#define STRHINTTYPE(t)  (t == HINT_ROUTE_TO_MASTER ? "HINT_ROUTE_TO_MASTER" : \
                         ((t) == HINT_ROUTE_TO_SLAVE ? "HINT_ROUTE_TO_SLAVE" : \
                          ((t) == HINT_ROUTE_TO_NAMED_SERVER ? "HINT_ROUTE_TO_NAMED_SERVER" : \
                           ((t) == HINT_ROUTE_TO_UPTODATE_SERVER ? "HINT_ROUTE_TO_UPTODATE_SERVER" : \
                            ((t) == HINT_ROUTE_TO_ALL ? "HINT_ROUTE_TO_ALL" : \
                             ((t) == HINT_PARAMETER ? "HINT_PARAMETER" : "UNKNOWN HINT TYPE"))))))

#define STRDCBREASON(r) ((r) == DCB_REASON_CLOSE ? "DCB_REASON_CLOSE" : \
                         ((r) == DCB_REASON_DRAINED ? "DCB_REASON_DRAINED" : \
                          ((r) == DCB_REASON_HIGH_WATER ? "DCB_REASON_HIGH_WATER" : \
                           ((r) == DCB_REASON_LOW_WATER ? "DCB_REASON_LOW_WATER" : \
                            ((r) == DCB_REASON_ERROR ? "DCB_REASON_ERROR" : \
                             ((r) == DCB_REASON_HUP ? "DCB_REASON_HUP" : \
                              ((r) == DCB_REASON_NOT_RESPONDING ? "DCB_REASON_NOT_RESPONDING" : \
                               "Unknown DCB reason")))))))

MXS_END_DECLS
