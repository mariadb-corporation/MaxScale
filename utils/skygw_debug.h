/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
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
 * Copyright SkySQL Ab 2013
 */


#include <assert.h>

#define __USE_UNIX98 1 
#include <pthread.h>
#include <unistd.h>

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
# undef ss_dassert
# undef ss_info_dassert 

#if !defined(ss_dassert)
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
#endif /* !defined(ss_dassert) */

#if !defined(ss_info_dassert)
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
#endif /* !defined(ss_info_dassert) */

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
    CHK_NUM_WRITEBUF
} skygw_chk_t;

# define STRBOOL(b) ((b) ? "TRUE" : "FALSE")
# define STRQTYPE(t) ((t) == QUERY_TYPE_WRITE ? "QUERY_TYPE_WRITE" :    \
                      ((t) == QUERY_TYPE_READ ? "QUERY_TYPE_READ" :     \
                       ((t) == QUERY_TYPE_SESSION_WRITE ? "QUERY_TYPE_SESSION_WRITE" : \
                        "QUERY_TYPE_UNKNOWN")))
#define STRLOGID(i) ((i) == LOGFILE_TRACE ? "LOGFILE_TRACE" :           \
                         ((i) == LOGFILE_MESSAGE ? "LOGFILE_MESSAGE" :  \
                              ((i) == LOGFILE_ERROR ? "LOGFILE_ERROR" : \
                                   "Unknown logfile type")))

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
    ss_info_dassert(c->slcursor_chk_top == CHK_NUM_SLIST_CURSOR &&      \
                    c->slcursor_chk_tail == CHK_NUM_SLIST_CURSOR,       \
                    "List cursor under- or overflow");                  \
    ss_info_dassert(c->slcursor_list != NULL,                           \
                    "List cursor doesn't have list");                   \
    ss_info_dassert(c->slcursor_pos != NULL ||                          \
                    (c->slcursor_pos == NULL &&                         \
                     c->slcursor_list->slist_head == NULL),             \
                    "List cursor doesn't have position");               \
    }

#define CHK_QUERY_TEST(q) {                                     \
      ss_info_dassert(q->qt_chk_top == CHK_NUM_QUERY_TEST &&    \
                      q->qt_chk_tail == CHK_NUM_QUERY_TEST,     \
                      "Query test under- or overflow.");        \
      }

#define CHK_LOGFILE(lf) {                                               \
              ss_info_dassert(lf->lf_chk_top == CHK_NUM_LOGFILE &&      \
                              lf->lf_chk_tail == CHK_NUM_LOGFILE,       \
                              "Logfile struct under- or overflow");     \
              ss_info_dassert(lf->lf_logpath != NULL &&                 \
              lf->lf_name_prefix != NULL &&                             \
              lf->lf_name_suffix != NULL &&                             \
              lf->lf_full_name != NULL,                                 \
              "NULL in name variable\n");                               \
              ss_info_dassert(lf->lf_id >= LOGFILE_FIRST &&             \
              lf->lf_id <= LOGFILE_LAST,                                \
              "Invalid logfile id\n");                                  \
              ss_info_dassert(lf->lf_writebuf_size > 0,                 \
                              "Error, logfile's writebuf size is zero " \
                              "or negative\n");                         \
                              (lf->lf_chk_top != CHK_NUM_LOGFILE ||     \
                               lf->lf_chk_tail != CHK_NUM_LOGFILE ?     \
                          FALSE :                                       \
                               (lf->lf_logpath == NULL ||               \
                               lf->lf_name_prefix == NULL ||            \
                               lf->lf_name_suffix == NULL ||            \
                               lf->lf_writebuf_size == 0 ||             \
                               lf->lf_full_name == NULL ? FALSE : TRUE)); \
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

#define CHK_WRITEBUF(w) {                                \
    ss_info_dassert(w->wb_chk_top == CHK_NUM_WRITEBUF,  \
                    "Writebuf under- or overflow");      \
    }
    
#endif /* SKYGW_DEBUG_H */
