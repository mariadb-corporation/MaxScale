#include <assert.h>

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

# define STRBOOL(b) ((b) ? "TRUE" : "FALSE")
# define STRQTYPE(t) ((t) == QUERY_TYPE_WRITE ? "QUERY_TYPE_WRITE" :    \
                      ((t) == QUERY_TYPE_READ ? "QUERY_TYPE_READ" :     \
                       ((t) == QUERY_TYPE_SESSION_WRITE ? "QUERY_TYPE_SESSION_WRITE" : \
                        "QUERY_TYPE_UNKNOWN")))
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

# define STRBOOL(b)
# define ss_dfprintf(a, b, ...)  
# define ss_dfflush  
# define ss_dfwrite  
# define ss_dassert
# define ss_info_dassert(exp, info)

#endif /* SS_DEBUG */

#define CHK_NUM_BASE 101

typedef enum skygw_chk_t {
    CHK_NUM_SLIST = CHK_NUM_BASE,
    CHK_NUM_SLIST_NODE,
    CHK_NUM_SLIST_CURSOR,
    CHK_NUM_QUERY_TEST,
    CHK_NUM_LOGFILE,
    CHK_NUM_FILEWRITER,
    CHK_NUM_THREAD,
    CHK_NUM_SIMPLE_MUTEX,
    CHK_NUM_MESSAGE              
} skygw_chk_t;

#define CHK_SLIST(l) {                                                            \
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

#define CHK_LOGFILE(lf) {                                           \
              ss_info_dassert(lf->lf_chk_top == CHK_NUM_LOGFILE &&  \
                              lf->lf_chk_tail == CHK_NUM_LOGFILE,   \
                              "Logfile struct under- or overflow"); \
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
        ss_info_dassert(mes->mes_chk_top == CHK_NUM_MESSAGE &&          \
                        mes->mes_chk_tail == CHK_NUM_MESSAGE,           \
                        "Message struct under- or overflow");           \
          } 
#endif /* SKYGW_DEBUG_H */
