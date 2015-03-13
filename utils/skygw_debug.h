/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2014
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <limits.h>

#define __USE_UNIX98 1 
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>

#if !defined(SKYGW_DEBUG_H)
#define SKYGW_DEBUG_H


#ifdef __cplusplus
#define EXTERN_C_BLOCK_BEGIN    extern "C" {
#define EXTERN_C_BLOCK_END      }
#define EXTERN_C_FUNC           extern "C"
#else
#define EXTERN_C_BLOCK_BEGIN
#define EXTERN_C_BLOCK_END
#define EXTERN_C_FUNC
#endif

#if defined(SS_DEBUG)
# define SS_PROF
#endif

#if defined(SS_DEBUG) || defined(SS_PROF)
# define ss_prof(exp) exp
#else
# define ss_prof(exp)
#endif /* SS_DEBUG || SS_PROF */

#if defined(SS_DEBUG)

# define ss_debug(exp) exp
# define ss_dfprintf fprintf
# define ss_dfflush  fflush
# define ss_dfwrite  fwrite

# define ss_dassert(exp)                                                \
    {                                                                   \
            if (!(exp)) {                                               \
                ss_dfprintf(stderr,                                     \
                            "debug assert %s:%d\n",                     \
                            (char*)__FILE__,                            \
                            __LINE__);                                  \
                ss_dfflush(stderr);                                     \
                assert(exp);                                            \
            }                                                           \
    }


# define ss_info_dassert(exp, info)                                     \
    {                                                                   \
            if (!(exp)) {                                               \
                ss_dfprintf(stderr, "debug assert %s:%d, %s\n",         \
                            (char *)__FILE__,                           \
                            __LINE__,                                   \
                            info);                                      \
                ss_dfflush(stderr);                                     \
                assert((exp));                                          \
            }                                                           \
    }

#else /* SS_DEBUG */

# define ss_debug(exp)
# define ss_dfprintf(a, b, ...)  
# define ss_dfflush(s) 
# define ss_dfwrite(a, b, c, d) 
# define ss_dassert(exp) 
# define ss_info_dassert(exp, info)

#endif /* SS_DEBUG */

#define CHK_NUM_BASE 101

typedef enum skygw_chk_t {
    CHK_NUM_SLIST = CHK_NUM_BASE,
    CHK_NUM_SLIST_NODE,
    CHK_NUM_SLIST_CURSOR,
    CHK_NUM_MLIST,
    CHK_NUM_MLIST_NODE,
    CHK_NUM_MLIST_CURSOR,
    CHK_NUM_QUERY_TEST,
    CHK_NUM_LOGFILE,
    CHK_NUM_FILEWRITER,
    CHK_NUM_THREAD,
    CHK_NUM_SIMPLE_MUTEX,
    CHK_NUM_MESSAGE,
    CHK_NUM_RWLOCK,
    CHK_NUM_FNAMES,
    CHK_NUM_LOGMANAGER,
    CHK_NUM_FILE,
    CHK_NUM_BLOCKBUF,
    CHK_NUM_HASHTABLE,
    CHK_NUM_DCB,
    CHK_NUM_PROTOCOL,
    CHK_NUM_SESSION,
    CHK_NUM_ROUTER_SES,
    CHK_NUM_MY_SESCMD,
    CHK_NUM_ROUTER_PROPERTY,
    CHK_NUM_SESCMD_CUR,
    CHK_NUM_BACKEND,
    CHK_NUM_BACKEND_REF,
    CHK_NUM_PREP_STMT,
    CHK_NUM_PINFO,
    CHK_NUM_MYSQLSES
} skygw_chk_t;

# define STRBOOL(b) ((b) ? "true" : "false")

# define STRQTYPE(t)	((t) == QUERY_TYPE_WRITE ? "QUERY_TYPE_WRITE" :    		\
			((t) == QUERY_TYPE_READ ? "QUERY_TYPE_READ" :     		\
			((t) == QUERY_TYPE_SESSION_WRITE ? "QUERY_TYPE_SESSION_WRITE" :	\
                        ((t) == QUERY_TYPE_UNKNOWN ? "QUERY_TYPE_UNKNOWN" : 		\
                        ((t) == QUERY_TYPE_LOCAL_READ ? "QUERY_TYPE_LOCAL_READ" :	\
                        ((t) == QUERY_TYPE_MASTER_READ ? "QUERY_TYPE_MASTER_READ" :	\
                        ((t) == QUERY_TYPE_USERVAR_READ ? "QUERY_TYPE_USERVAR_READ" :	\
                        ((t) == QUERY_TYPE_SYSVAR_READ ? "QUERY_TYPE_SYSVAR_READ" :	\
                        ((t) == QUERY_TYPE_GSYSVAR_READ ? "QUERY_TYPE_GSYSVAR_READ" :	\
                        ((t) == QUERY_TYPE_GSYSVAR_WRITE ? "QUERY_TYPE_GSYSVAR_WRITE" :	\
                        ((t) == QUERY_TYPE_BEGIN_TRX ? "QUERY_TYPE_BEGIN_TRX" :		\
                        ((t) == QUERY_TYPE_ENABLE_AUTOCOMMIT ? "QUERY_TYPE_ENABLE_AUTOCOMMIT" :		\
                        ((t) == QUERY_TYPE_DISABLE_AUTOCOMMIT ? "QUERY_TYPE_DISABLE_AUTOCOMMIT" : 	\
                        ((t) == QUERY_TYPE_ROLLBACK ? "QUERY_TYPE_ROLLBACK" : 		\
                        ((t) == QUERY_TYPE_COMMIT ? "QUERY_TYPE_COMMIT" : 		\
                        ((t) == QUERY_TYPE_PREPARE_NAMED_STMT ? "QUERY_TYPE_PREPARE_NAMED_STMT" :	\
                        ((t) == QUERY_TYPE_PREPARE_STMT ? "QUERY_TYPE_PREPARE_STMT" :	\
                        ((t) == QUERY_TYPE_EXEC_STMT ? "QUERY_TYPE_EXEC_STMT" :		\
                        ((t) == QUERY_TYPE_CREATE_TMP_TABLE ? "QUERY_TYPE_CREATE_TMP_TABLE" :		\
                        ((t) == QUERY_TYPE_READ_TMP_TABLE ? "QUERY_TYPE_READ_TMP_TABLE" :		\
                        ((t) == QUERY_TYPE_SHOW_DATABASES ? "QUERY_TYPE_SHOW_DATABASES" :		\
                        ((t) == QUERY_TYPE_SHOW_TABLES ? "QUERY_TYPE_SHOW_TABLES" :	\
                        "Unknown query type"))))))))))))))))))))))

#define STRLOGID(i) ((i) == LOGFILE_TRACE ? "LOGFILE_TRACE" :           \
                ((i) == LOGFILE_MESSAGE ? "LOGFILE_MESSAGE" :           \
                 ((i) == LOGFILE_ERROR ? "LOGFILE_ERROR" :              \
                  ((i) == LOGFILE_DEBUG ? "LOGFILE_DEBUG" :             \
                   "Unknown logfile type"))))
                   
#define STRLOGNAME(n) ((n) == LOGFILE_TRACE ? "Trace log" :		\
			((n) == LOGFILE_MESSAGE ? "Message log" :	\
			((n) == LOGFILE_ERROR ? "Error log" :		\
			((n) == LOGFILE_DEBUG ? "Debug log" :		\
			"Unknown log file type"))))

#define STRPACKETTYPE(p) ((p) == MYSQL_COM_INIT_DB ? "COM_INIT_DB" :          \
                          ((p) == MYSQL_COM_CREATE_DB ? "COM_CREATE_DB" :     \
                           ((p) == MYSQL_COM_DROP_DB ? "COM_DROP_DB" :        \
                            ((p) == MYSQL_COM_REFRESH ? "COM_REFRESH" :       \
                             ((p) == MYSQL_COM_DEBUG ? "COM_DEBUG" :          \
                              ((p) == MYSQL_COM_PING ? "COM_PING" :           \
                               ((p) == MYSQL_COM_CHANGE_USER ? "COM_CHANGE_USER" : \
                                ((p) == MYSQL_COM_QUERY ? "COM_QUERY" :       \
                                 ((p) == MYSQL_COM_SHUTDOWN ? "COM_SHUTDOWN" : \
                                  ((p) == MYSQL_COM_PROCESS_INFO ? "COM_PROCESS_INFO" :                 \
                                   ((p) == MYSQL_COM_CONNECT ? "COM_CONNECT" :                          \
                                    ((p) == MYSQL_COM_PROCESS_KILL ? "COM_PROCESS_KILL" :               \
                                     ((p) == MYSQL_COM_TIME ? "COM_TIME" :                              \
                                      ((p) == MYSQL_COM_DELAYED_INSERT ? "COM_DELAYED_INSERT" :         \
                                       ((p) == MYSQL_COM_DAEMON ? "COM_DAEMON" :                        \
                                        ((p) == MYSQL_COM_QUIT ? "COM_QUIT" :                           \
                                         ((p) == MYSQL_COM_STMT_PREPARE ? "MYSQL_COM_STMT_PREPARE" :    \
                                          ((p) == MYSQL_COM_STMT_EXECUTE ? "MYSQL_COM_STMT_EXECUTE" :   \
                                          "UNKNOWN MYSQL PACKET TYPE"))))))))))))))))))

#define STRDCBSTATE(s) ((s) == DCB_STATE_ALLOC ? "DCB_STATE_ALLOC" :    \
                        ((s) == DCB_STATE_POLLING ? "DCB_STATE_POLLING" : \
                         ((s) == DCB_STATE_LISTENING ? "DCB_STATE_LISTENING" : \
                          ((s) == DCB_STATE_DISCONNECTED ? "DCB_STATE_DISCONNECTED" : \
                           ((s) == DCB_STATE_NOPOLLING ? "DCB_STATE_NOPOLLING" : \
                            ((s) == DCB_STATE_FREED ? "DCB_STATE_FREED" : \
                             ((s) == DCB_STATE_ZOMBIE ? "DCB_STATE_ZOMBIE" : \
                              ((s) == DCB_STATE_UNDEFINED ? "DCB_STATE_UNDEFINED" : "DCB_STATE_UNKNOWN"))))))))

#define STRSESSIONSTATE(s) ((s) == SESSION_STATE_ALLOC ? "SESSION_STATE_ALLOC" : \
                            ((s) == SESSION_STATE_READY ? "SESSION_STATE_READY" : \
                             ((s) == SESSION_STATE_LISTENER ? "SESSION_STATE_LISTENER" : \
                              ((s) == SESSION_STATE_LISTENER_STOPPED ? "SESSION_STATE_LISTENER_STOPPED" : \
                               "SESSION_STATE_UNKNOWN"))))

#define STRPROTOCOLSTATE(s) ((s) == MYSQL_ALLOC ? "MYSQL_ALLOC" :       \
        ((s) == MYSQL_PENDING_CONNECT ? "MYSQL_PENDING_CONNECT" :       \
        ((s) == MYSQL_CONNECTED ? "MYSQL_CONNECTED" :                   \
        ((s) == MYSQL_AUTH_SENT ? "MYSQL_AUTH_SENT" :                   \
        ((s) == MYSQL_AUTH_RECV ? "MYSQL_AUTH_RECV" :                   \
        ((s) == MYSQL_AUTH_FAILED ? "MYSQL_AUTH_FAILED" :               \
        ((s) == MYSQL_IDLE ? "MYSQL_IDLE" :                             \
        "UNKNOWN MYSQL STATE")))))))

#define STRITEMTYPE(t) ((t) == Item::FIELD_ITEM ? "FIELD_ITEM" :      \
        ((t) == Item::FUNC_ITEM ? "FUNC_ITEM" :                       \
        ((t) == Item::SUM_FUNC_ITEM ? "SUM_FUNC_ITEM" :               \
        ((t) == Item::STRING_ITEM ? "STRING_ITEM" :                   \
        ((t) == Item::INT_ITEM ? "INT_ITEM" :                         \
        ((t) == Item::REAL_ITEM ? "REAL_ITEM" :                       \
        ((t) == Item::NULL_ITEM ? "NULL_ITEM" :                       \
        ((t) == Item::VARBIN_ITEM ? "VARBIN_ITEM" :                   \
        ((t) == Item::COPY_STR_ITEM ? "COPY_STR_ITEM" :               \
         ((t) == Item::FIELD_AVG_ITEM ? "FIELD_AVG_ITEM" :            \
          ((t) == Item::DEFAULT_VALUE_ITEM ? "DEFAULT_VALUE_ITEM" :   \
           ((t) == Item::PROC_ITEM ? "PROC_ITEM" :                    \
            ((t) == Item::COND_ITEM ? "COND_ITEM" :                   \
             ((t) == Item::REF_ITEM ? "REF_ITEM" :                    \
              (t) == Item::FIELD_STD_ITEM ? "FIELD_STD_ITEM" :        \
              ((t) == Item::FIELD_VARIANCE_ITEM ? "FIELD_VARIANCE_ITEM" :     \
               ((t) == Item::INSERT_VALUE_ITEM ? "INSERT_VALUE_ITEM":         \
                ((t) == Item::SUBSELECT_ITEM ? "SUBSELECT_ITEM" :             \
                 ((t) == Item::ROW_ITEM ? "ROW_ITEM" :                        \
                  ((t) == Item::CACHE_ITEM ? "CACHE_ITEM" :                   \
                   ((t) == Item::TYPE_HOLDER ? "TYPE_HOLDER" :                \
                    ((t) == Item::PARAM_ITEM ? "PARAM_ITEM" :                 \
                     ((t) == Item::TRIGGER_FIELD_ITEM ? "TRIGGER_FIELD_ITEM" : \
                      ((t) == Item::DECIMAL_ITEM ? "DECIMAL_ITEM" :           \
                       ((t) == Item::XPATH_NODESET ? "XPATH_NODESET" :        \
                        ((t) == Item::XPATH_NODESET_CMP ? "XPATH_NODESET_CMP" : \
                         ((t) == Item::VIEW_FIXER_ITEM ? "VIEW_FIXER_ITEM" :  \
                          ((t) == Item::EXPR_CACHE_ITEM ? "EXPR_CACHE_ITEM" : \
                           "Unknown item")))))))))))))))))))))))))))
         
#define STRDCBROLE(r) ((r) == DCB_ROLE_SERVICE_LISTENER ? "DCB_ROLE_SERVICE_LISTENER" : \
                       ((r) == DCB_ROLE_REQUEST_HANDLER ? "DCB_ROLE_REQUEST_HANDLER" : \
                        "UNKNOWN DCB ROLE"))

#define STRBETYPE(t) ((t) == BE_MASTER ? "BE_MASTER" : \
                        ((t) == BE_SLAVE ? "BE_SLAVE" : \
                        ((t) == BE_UNDEFINED ? "BE_UNDEFINED" : \
                        "Unknown backend tpe")))
                        
#define STRCRITERIA(c) ((c) == UNDEFINED_CRITERIA ? "UNDEFINED_CRITERIA" :              \
                        ((c) == LEAST_GLOBAL_CONNECTIONS ? "LEAST_GLOBAL_CONNECTIONS" : \
                        ((c) == LEAST_ROUTER_CONNECTIONS ? "LEAST_ROUTER_CONNECTIONS" : \
                        ((c) == LEAST_BEHIND_MASTER ? "LEAST_BEHIND_MASTER"           : \
                        ((c) == LEAST_CURRENT_OPERATIONS ? "LEAST_CURRENT_OPERATIONS" : "Unknown criteria")))))

#define STRSRVSTATUS(s) (SERVER_IS_MASTER(s)  ? "RUNNING MASTER" :     \
                        (SERVER_IS_SLAVE(s)   ? "RUNNING SLAVE" :       \
                        (SERVER_IS_JOINED(s)  ? "RUNNING JOINED" :     \
                        (SERVER_IS_NDB(s)     ? "RUNNING NDB" :     	\
                        ((SERVER_IS_RUNNING(s) && SERVER_IN_MAINT(s)) ? "RUNNING MAINTENANCE" : \
                        (SERVER_IS_RELAY_SERVER(s) ? "RUNNING RELAY" : \
                        (SERVER_IS_RUNNING(s) ? "RUNNING (only)" : "NO STATUS")))))))

#define STRTARGET(t)	(t == TARGET_ALL ? "TARGET_ALL" :			\
			(t == TARGET_MASTER ? "TARGET_MASTER" : 		\
			(t == TARGET_SLAVE ? "TARGET_SLAVE" : 			\
			(t == TARGET_NAMED_SERVER ? "TARGET_NAMED_SERVER" : 	\
			(t == TARGET_UNDEFINED ? "TARGET_UNDEFINED" : 		\
			"Unknown target value")))))
                        
#define BREFSRV(b)	(b->bref_backend->backend_server)
                        
                        
#define STRHINTTYPE(t)	(t == HINT_ROUTE_TO_MASTER ? "HINT_ROUTE_TO_MASTER" :	\
			((t) == HINT_ROUTE_TO_SLAVE ? "HINT_ROUTE_TO_SLAVE" :	\
			((t) == HINT_ROUTE_TO_NAMED_SERVER ? "HINT_ROUTE_TO_NAMED_SERVER" :		\
			((t) == HINT_ROUTE_TO_UPTODATE_SERVER ? "HINT_ROUTE_TO_UPTODATE_SERVER" :	\
			((t) == HINT_ROUTE_TO_ALL ? "HINT_ROUTE_TO_ALL" : 	\
			((t) == HINT_PARAMETER ? "HINT_PARAMETER" : "UNKNOWN HINT TYPE"))))))
                        
#define STRDCBREASON(r)	((r) == DCB_REASON_CLOSE ? "DCB_REASON_CLOSE" : 		\
			((r) == DCB_REASON_DRAINED ? "DCB_REASON_DRAINED" : 		\
			((r) == DCB_REASON_HIGH_WATER ? "DCB_REASON_HIGH_WATER" : 	\
			((r) == DCB_REASON_LOW_WATER ? "DCB_REASON_LOW_WATER" : 	\
			((r) == DCB_REASON_ERROR ? "DCB_REASON_ERROR" : 		\
			((r) == DCB_REASON_HUP ? "DCB_REASON_HUP" :			\
			((r) == DCB_REASON_NOT_RESPONDING ? "DCB_REASON_NOT_RESPONDING" : 	\
			"Unknown DCB reason")))))))
                        
#define CHK_MLIST(l) {                                                  \
            ss_info_dassert((l->mlist_chk_top ==  CHK_NUM_MLIST &&      \
                             l->mlist_chk_tail == CHK_NUM_MLIST),       \
                            "Single-linked list structure under- or overflow"); \
            if (l->mlist_first == NULL) {                                \
                ss_info_dassert(l->mlist_nodecount == 0,                   \
                                "List head is NULL but element counter is not zero."); \
                ss_info_dassert(l->mlist_last == NULL,                  \
                                "List head is NULL but tail has node"); \
            } else {                                                    \
                ss_info_dassert(l->mlist_nodecount > 0,                    \
                                "List head has node but element counter is not " \
                                "positive.");                           \
                CHK_MLIST_NODE(l->mlist_first);                         \
                CHK_MLIST_NODE(l->mlist_last);                          \
            }                                                           \
            if (l->mlist_nodecount == 0) {                                 \
                ss_info_dassert(l->mlist_first == NULL,                 \
                                "Element counter is zero but head has node"); \
                ss_info_dassert(l->mlist_last == NULL,                  \
                                "Element counter is zero but tail has node"); \
            }                                                           \
    }



#define CHK_MLIST_NODE(n) {                                             \
            ss_info_dassert((n->mlnode_chk_top == CHK_NUM_MLIST_NODE && \
                             n->mlnode_chk_tail == CHK_NUM_MLIST_NODE), \
                            "Single-linked list node under- or overflow"); \
    }

#define CHK_MLIST_CURSOR(c) {                                           \
    ss_info_dassert(c->mlcursor_chk_top == CHK_NUM_MLIST_CURSOR &&      \
                    c->mlcursor_chk_tail == CHK_NUM_MLIST_CURSOR,       \
                    "List cursor under- or overflow");                  \
    ss_info_dassert(c->mlcursor_list != NULL,                           \
                    "List cursor doesn't have list");                   \
    ss_info_dassert(c->mlcursor_pos != NULL ||                          \
                    (c->mlcursor_pos == NULL &&                         \
                     c->mlcursor_list->mlist_first == NULL),            \
                    "List cursor doesn't have position");               \
    }

#define CHK_SLIST(l) { \
    ss_info_dassert((l->slist_chk_top ==  CHK_NUM_SLIST &&              \
                     l->slist_chk_tail == CHK_NUM_SLIST),               \
                    "Single-linked list structure under- or overflow"); \
    if (l->slist_head == NULL) {                                        \
        ss_info_dassert(l->slist_nelems == 0,                           \
                        "List head is NULL but element counter is not zero."); \
        ss_info_dassert(l->slist_tail == NULL,                          \
                        "List head is NULL but tail has node");         \
    } else {                                                            \
        ss_info_dassert(l->slist_nelems > 0,                            \
                        "List head has node but element counter is not " \
                        "positive.");                                   \
        CHK_SLIST_NODE(l->slist_head);                                  \
        CHK_SLIST_NODE(l->slist_tail);                                  \
    }                                                                   \
    if (l->slist_nelems == 0) {                                         \
        ss_info_dassert(l->slist_head == NULL,                          \
                        "Element counter is zero but head has node");   \
        ss_info_dassert(l->slist_tail == NULL,                          \
                        "Element counter is zero but tail has node");   \
    }                                                                   \
    }



#define CHK_SLIST_NODE(n) {                                             \
            ss_info_dassert((n->slnode_chk_top == CHK_NUM_SLIST_NODE && \
                             n->slnode_chk_tail == CHK_NUM_SLIST_NODE), \
                            "Single-linked list node under- or overflow"); \
          }

#define CHK_SLIST_CURSOR(c) {                                           \
                  ss_info_dassert(c->slcursor_chk_top == CHK_NUM_SLIST_CURSOR && \
                                  c->slcursor_chk_tail == CHK_NUM_SLIST_CURSOR, \
                    "List cursor under- or overflow");                  \
    ss_info_dassert(c->slcursor_list != NULL,                           \
                    "List cursor doesn't have list");                   \
    ss_info_dassert(c->slcursor_pos != NULL ||                          \
                    (c->slcursor_pos == NULL &&                         \
                     c->slcursor_list->slist_head == NULL),             \
                    "List cursor doesn't have position");               \
          }

#define CHK_QUERY_TEST(q) {                                             \
                ss_info_dassert(q->qt_chk_top == CHK_NUM_QUERY_TEST &&  \
                                q->qt_chk_tail == CHK_NUM_QUERY_TEST,   \
                                "Query test under- or overflow.");      \
        }

#define CHK_LOGFILE(lf) {                                               \
                  ss_info_dassert(lf->lf_chk_top == CHK_NUM_LOGFILE &&  \
                              lf->lf_chk_tail == CHK_NUM_LOGFILE,       \
                              "Logfile struct under- or overflow");     \
              ss_info_dassert(lf->lf_filepath != NULL &&                \
                              lf->lf_name_prefix != NULL &&             \
                              lf->lf_name_suffix != NULL &&             \
                              lf->lf_full_file_name != NULL,                \
                              "NULL in name variable\n");               \
              ss_info_dassert(lf->lf_id >= LOGFILE_FIRST &&             \
                              lf->lf_id <= LOGFILE_LAST,                \
                              "Invalid logfile id\n");                  \
              ss_debug(                                                 \
              (lf->lf_chk_top != CHK_NUM_LOGFILE ||                     \
               lf->lf_chk_tail != CHK_NUM_LOGFILE ?                     \
               false :                                                  \
               (lf->lf_filepath == NULL ||                              \
                lf->lf_name_prefix == NULL ||                           \
                lf->lf_name_suffix == NULL ||                           \
                lf->lf_full_file_name == NULL ? false : true));)        \
          }

#define CHK_FILEWRITER(fwr) {                                           \
            ss_info_dassert(fwr->fwr_chk_top == CHK_NUM_FILEWRITER &&   \
                            fwr->fwr_chk_tail == CHK_NUM_FILEWRITER,    \
                            "File writer struct under- or overflow");   \
    }

#define CHK_THREAD(thr) {                                               \
            ss_info_dassert(thr->sth_chk_top == CHK_NUM_THREAD &&       \
                                thr->sth_chk_tail == CHK_NUM_THREAD,    \
                                "Thread struct under- or overflow");    \
            }

#define CHK_SIMPLE_MUTEX(sm) {                                          \
            ss_info_dassert(sm->sm_chk_top == CHK_NUM_SIMPLE_MUTEX &&   \
                                sm->sm_chk_tail == CHK_NUM_SIMPLE_MUTEX, \
                                "Simple mutex struct under- or overflow"); \
    }

#define CHK_MESSAGE(mes) {                                              \
            ss_info_dassert(mes->mes_chk_top == CHK_NUM_MESSAGE &&      \
                            mes->mes_chk_tail == CHK_NUM_MESSAGE,       \
                            "Message struct under- or overflow");       \
    }


#define CHK_MLIST_ISLOCKED(l) {                                         \
    ss_info_dassert((l.mlist_uselock && l.mlist_islocked) ||            \
                    !(l.mlist_uselock || l.mlist_islocked),             \
                        ("mlist is not locked although it should."));   \
    CHK_MUTEXED_FOR_THR(l.mlist_uselock,l.mlist_rwlock);               \
    }

#define CHK_MUTEXED_FOR_THR(b,l) {                                      \
        ss_info_dassert(!b ||                                           \
            (b && (l->srw_rwlock_thr == pthread_self())),               \
            "rwlock is not acquired although it should be.");           \
    }

#define CHK_FNAMES_CONF(fn) {                                           \
            ss_info_dassert(fn->fn_chk_top == CHK_NUM_FNAMES &&         \
                            fn->fn_chk_tail == CHK_NUM_FNAMES,          \
                            "File names confs struct under- or overflow"); \
    }

#define CHK_LOGMANAGER(lm) {                                            \
    ss_info_dassert(lm->lm_chk_top == CHK_NUM_LOGMANAGER &&             \
                        lm->lm_chk_tail == CHK_NUM_LOGMANAGER,          \
                        "Log manager struct under- or overflow");       \
    }
    
#define CHK_FILE(f) {                                                   \
        ss_info_dassert(f->sf_chk_top == CHK_NUM_FILE &&                \
        f->sf_chk_tail == CHK_NUM_FILE,                                 \
                        "File struct under- or overflow");              \
    }


#define CHK_BLOCKBUF(bb) {                                      \
            ss_info_dassert(bb->bb_chk_top == CHK_NUM_BLOCKBUF, \
                            "Block buf under- or overflow");    \
    }

#define CHK_HASHTABLE(t) {                                  \
    ss_info_dassert(t->ht_chk_top == CHK_NUM_HASHTABLE &&   \
                    t->ht_chk_tail == CHK_NUM_HASHTABLE,    \
                    "Hashtable under- or overflow");        \
    }

#define CHK_DCB(d) {                                            \
        ss_info_dassert(d->dcb_chk_top == CHK_NUM_DCB &&        \
                d->dcb_chk_tail == CHK_NUM_DCB,                 \
                        "Dcb under- or overflow");              \
        }

#define CHK_PROTOCOL(p) {                                            \
            ss_info_dassert(p->protocol_chk_top == CHK_NUM_PROTOCOL &&  \
                            p->protocol_chk_tail == CHK_NUM_PROTOCOL,   \
                            "Protocol under- or overflow");             \
    }

#define CHK_SESSION(s) {                                          \
            ss_info_dassert(s->ses_chk_top == CHK_NUM_SESSION &&  \
                            s->ses_chk_tail == CHK_NUM_SESSION,         \
                            "Session under- or overflow");              \
    }

#define CHK_GWBUF(b) {                                                  \
            ss_info_dassert(((char *)(b)->start <= (char *)(b)->end),   \
                            "gwbuf start has passed the endpoint");     \
    }

#define CHK_CLIENT_RSES(r) {                                            \
                ss_info_dassert((r)->rses_chk_top == CHK_NUM_ROUTER_SES && \
                                (r)->rses_chk_tail == CHK_NUM_ROUTER_SES, \
                                "Router client session has invalid check fields"); \
        }

#define CHK_RSES_PROP(p) {                                            \
        ss_info_dassert((p)->rses_prop_chk_top == CHK_NUM_ROUTER_PROPERTY && \
		(p)->rses_prop_chk_tail == CHK_NUM_ROUTER_PROPERTY, \
		"Router property has invalid check fields"); \
        }
        
#define CHK_MYSQL_SESCMD(s) {                                            \
	ss_info_dassert((s)->my_sescmd_chk_top == CHK_NUM_MY_SESCMD && \
		(s)->my_sescmd_chk_tail == CHK_NUM_MY_SESCMD, \
		"Session command has invalid check fields"); \
        }
        
#define CHK_SESCMD_CUR(c) {     \
        ss_info_dassert((c)->scmd_cur_chk_top == CHK_NUM_SESCMD_CUR && \
                (c)->scmd_cur_chk_tail == CHK_NUM_SESCMD_CUR, \
                "Session command cursor has invalid check fields"); \
        }
        
#define CHK_BACKEND(b) {     \
        ss_info_dassert((b)->be_chk_top == CHK_NUM_BACKEND && \
        (b)->be_chk_tail == CHK_NUM_BACKEND, \
        "BACKEND has invalid check fields"); \
}

#define CHK_BACKEND_REF(r) {                                            \
        ss_info_dassert((r)->bref_chk_top == CHK_NUM_BACKEND_REF &&     \
        (r)->bref_chk_tail == CHK_NUM_BACKEND_REF,                      \
        "Backend reference has invalid check fields");                  \
}

#define CHK_PREP_STMT(p) {                                              \
        ss_info_dassert((p)->pstmt_chk_top == CHK_NUM_PREP_STMT &&      \
        (p)->pstmt_chk_tail == CHK_NUM_PREP_STMT,                       \
        "Prepared statement struct has invalid check fields");          \
}

#define CHK_PARSING_INFO(p) {                                           \
        ss_info_dassert((p)->pi_chk_top == CHK_NUM_PINFO &&            \
        (p)->pi_chk_tail == CHK_NUM_PINFO,                              \
        "Parsing info struct has invalid check fields");                \
}

#define CHK_MYSQL_SESSION(s) {						\
	ss_info_dassert((s)->myses_chk_top == CHK_NUM_MYSQLSES &&	\
	(s)->myses_chk_tail == CHK_NUM_MYSQLSES,			\
	"MYSQL session struct has invalid check fields");		\
}


#if defined(FAKE_CODE)
bool conn_open[10240];
#endif /* FAKE_CODE */

#endif /* SKYGW_DEBUG_H */
