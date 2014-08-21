#if !defined(SKYGW_UTILS_H)
#define SKYGW_UTILS_H

#define MLIST
#ifndef MIN
#define MIN(a,b) (a<b ? a : b)
#endif
#ifndef MAX
#define MAX(a,b) (a>b ? a : b)
#endif
#define FSYNCLIMIT 10

#include "skygw_types.h"
#include "skygw_debug.h"

#define DISKWRITE_LATENCY (5*MSEC_USEC)

typedef struct slist_node_st    slist_node_t;
typedef struct slist_st         slist_t;
typedef struct slist_cursor_st  slist_cursor_t;
typedef struct mlist_node_st    mlist_node_t;
typedef struct skygw_file_st    skygw_file_t;
typedef struct skygw_thread_st  skygw_thread_t;
typedef struct skygw_message_st skygw_message_t;

typedef struct simple_mutex_st {
        skygw_chk_t      sm_chk_top;
        pthread_mutex_t  sm_mutex;
        pthread_t        sm_lock_thr;
        bool             sm_locked;
        int              sm_enabled; /**< defined as in to minimize mutexing */
        bool             sm_flat;
        char*            sm_name;
        skygw_chk_t      sm_chk_tail;
} simple_mutex_t;

typedef struct skygw_rwlock_st {
        skygw_chk_t       srw_chk_top;
        pthread_rwlock_t* srw_rwlock;
        pthread_t         srw_rwlock_thr;
        skygw_chk_t       srw_chk_tail;
} skygw_rwlock_t;


typedef struct mlist_st {
        skygw_chk_t        mlist_chk_top;
        char*              mlist_name;
        void (*mlist_datadel)(void *);  /**< clean-up function for data */
        simple_mutex_t     mlist_mutex; /**< protect node updates and clean-up */
        bool               mlist_uselock;
        bool               mlist_islocked;
        bool               mlist_deleted;
        size_t             mlist_nodecount;
        size_t             mlist_nodecount_max; /**< size limit. 0 == no limit */
#if 1
        size_t             mlist_versno;
#endif
        bool               mlist_flat;
        mlist_node_t*      mlist_first;
        mlist_node_t*      mlist_last;
        skygw_chk_t        mlist_chk_tail;
} mlist_t;

typedef struct mlist_cursor_st {
        skygw_chk_t     mlcursor_chk_top;
        mlist_t*        mlcursor_list;
        mlist_node_t*   mlcursor_pos;
        pthread_t*      mlcursor_owner_thr;
        skygw_chk_t     mlcursor_chk_tail;
} mlist_cursor_t;

struct mlist_node_st {
        skygw_chk_t   mlnode_chk_top;
        mlist_t*      mlnode_list;
        mlist_node_t* mlnode_next;
        void*         mlnode_data;
        bool          mlnode_deleted;
        skygw_chk_t   mlnode_chk_tail;
};


typedef enum { THR_INIT, THR_RUNNING, THR_STOPPED, THR_DONE } skygw_thr_state_t;
typedef enum { MES_RC_FAIL, MES_RC_SUCCESS, MES_RC_TIMEOUT } skygw_mes_rc_t;

EXTERN_C_BLOCK_BEGIN
slist_cursor_t* slist_init(void);
void slist_done(slist_cursor_t* c);

void slcursor_add_data(slist_cursor_t* c, void* data);
void* slcursor_get_data(slist_cursor_t* c);

bool slcursor_move_to_begin(slist_cursor_t* c);
bool slcursor_step_ahead(slist_cursor_t* c);

EXTERN_C_BLOCK_END

mlist_t*      mlist_init(mlist_t*         mlist,
                         mlist_cursor_t** cursor,
                         char*            name,
                         void           (*datadel)(void*),
                         int              maxnodes);

void          mlist_done(mlist_t* list);
bool          mlist_add_data_nomutex(mlist_t* list, void* data);
bool          mlist_add_node_nomutex(mlist_t* list, mlist_node_t* newnode);
void*         mlist_node_get_data(mlist_node_t* node);
mlist_node_t* mlist_detach_nodes(mlist_t* ml);
mlist_node_t* mlist_detach_first(mlist_t* ml);
void          mlist_node_done(mlist_node_t* n);

int             mlist_cursor_done(mlist_cursor_t* c);
mlist_cursor_t* mlist_cursor_init(mlist_t* ml);
void            mlist_cursor_add_data(mlist_cursor_t* c, void* data);
void*           mlist_cursor_get_data_nomutex(mlist_cursor_t* c);
bool            mlist_cursor_move_to_first(mlist_cursor_t* c);
bool            mlist_cursor_step_ahead(mlist_cursor_t* c);

/** Skygw thread routines */
skygw_thread_t*   skygw_thread_init(
        const char* name,
        void* (*sth_thrfun)(void* data),
        void* data);
void              skygw_thread_done(skygw_thread_t* th);
int               skygw_thread_start(skygw_thread_t* thr);
skygw_thr_state_t skygw_thread_get_state(skygw_thread_t* thr);
pthread_t         skygw_thread_gettid(skygw_thread_t* thr);

int get_timestamp_len(void);
int snprint_timestamp(char* p_ts, int tslen);

EXTERN_C_BLOCK_BEGIN

void skygw_thread_set_state(
        skygw_thread_t*  thr,
        skygw_thr_state_t state);
void* skygw_thread_get_data(skygw_thread_t* thr);
bool skygw_thread_must_exit(skygw_thread_t* thr);
bool skygw_thread_set_exitflag(
        skygw_thread_t* thr,
        skygw_message_t* sendmes,
        skygw_message_t* recmes);

EXTERN_C_BLOCK_END

/** Skygw thread routines */

/** Skygw file routines */
skygw_file_t* skygw_file_init(char* fname, char* symlinkname);
void skygw_file_done(skygw_file_t* file);
bool skygw_file_write(
        skygw_file_t* file,
        void*         data,
        size_t        nbytes,
        bool          flush);
/** Skygw file routines */

EXTERN_C_BLOCK_BEGIN

void acquire_lock(int* l);
void release_lock(int* l);

simple_mutex_t* simple_mutex_init(simple_mutex_t* mutexptr, const char* name);
int simple_mutex_done(simple_mutex_t* sm);
int simple_mutex_lock(simple_mutex_t* sm, bool block);
int simple_mutex_unlock(simple_mutex_t* sm);

/** Skygw message routines */
skygw_message_t* skygw_message_init(void);

void skygw_message_done(
        skygw_message_t* mes);

skygw_mes_rc_t skygw_message_send(
        skygw_message_t* mes);

void skygw_message_wait(
        skygw_message_t* mes);

skygw_mes_rc_t skygw_message_request(
        skygw_message_t* mes);
        
void skygw_message_reset(
        skygw_message_t* mes);
/** Skygw message routines */

EXTERN_C_BLOCK_END

int skygw_rwlock_wrlock(skygw_rwlock_t* rwlock);
int skygw_rwlock_rdlock(skygw_rwlock_t* rwlock);
int skygw_rwlock_unlock(skygw_rwlock_t* rwlock);
int skygw_rwlock_init(skygw_rwlock_t** rwlock);

int atomic_add(int *variable, int value);

EXTERN_C_BLOCK_BEGIN

char* replace_literal(char* haystack, 
                      const char* needle, 
                      const char* replacement);

EXTERN_C_BLOCK_END

#endif /* SKYGW_UTILS_H */
