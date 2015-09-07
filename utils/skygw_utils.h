#if !defined(SKYGW_UTILS_H)
#define SKYGW_UTILS_H

/*
 * We need a common.h file that is included by every component.
 */
#if !defined(STRERROR_BUFLEN)
#define STRERROR_BUFLEN 512
#endif

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
        size_t             mlist_versno;
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


static const char*  timestamp_formatstr = "%04d-%02d-%02d %02d:%02d:%02d   ";
/** One for terminating '\0' */
static const size_t    timestamp_len       =    (4+1 +2+1 +2+1 +2+1 +2+1 +2+3  +1) * sizeof(char);


static const char*  timestamp_formatstr_hp = "%04d-%02d-%02d %02d:%02d:%02d.%03d   ";
/** One for terminating '\0' */
static const size_t    timestamp_len_hp       =    (4+1 +2+1 +2+1 +2+1 +2+1 +2+1+3+3  +1) * sizeof(char);

/** Single-linked list for storing test cases */

struct slist_node_st {
        skygw_chk_t   slnode_chk_top;
        slist_t*      slnode_list;
        slist_node_t* slnode_next;
        void*         slnode_data;
        size_t        slnode_cursor_refcount;
        skygw_chk_t   slnode_chk_tail;
};

struct slist_st {
        skygw_chk_t   slist_chk_top;
        slist_node_t* slist_head;
        slist_node_t* slist_tail;
        int        slist_nelems;
        slist_t*      slist_cursors_list;
        skygw_chk_t   slist_chk_tail;
};

struct slist_cursor_st {
        skygw_chk_t     slcursor_chk_top;
        slist_t*        slcursor_list;
        slist_node_t*   slcursor_pos;
        skygw_chk_t     slcursor_chk_tail;
};

struct skygw_thread_st {
        skygw_chk_t       sth_chk_top;
        bool              sth_must_exit;
        simple_mutex_t*   sth_mutex;
        pthread_t         sth_parent;
        pthread_t         sth_thr;
        int               sth_errno;
#if defined(SS_DEBUG)
        skygw_thr_state_t sth_state;
#endif
        char*             sth_name;
        void* (*sth_thrfun)(void* data);
        void*             sth_data;
        skygw_chk_t       sth_chk_tail;
};

struct skygw_message_st {
        skygw_chk_t     mes_chk_top;
        bool            mes_sent;
        pthread_mutex_t mes_mutex;
        pthread_cond_t  mes_cond;
        skygw_chk_t     mes_chk_tail;
};

struct skygw_file_st {
        skygw_chk_t  sf_chk_top;
        char*        sf_fname;
        FILE*        sf_file;
        int          sf_fd;
        skygw_chk_t  sf_chk_tail;
};

EXTERN_C_BLOCK_BEGIN

slist_cursor_t* slist_init(void);
void slist_done(slist_cursor_t* c);
size_t slist_size(slist_cursor_t* c);
void slcursor_add_data(slist_cursor_t* c, void* data);
void slcursor_remove_data(slist_cursor_t* c);
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

size_t get_timestamp_len(void);
size_t get_timestamp_len_hp(void);
size_t snprint_timestamp(char* p_ts, size_t tslen);
size_t snprint_timestamp_hp(char* p_ts, size_t tslen);

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
skygw_file_t* skygw_file_alloc(char* fname);
void skygw_file_free(skygw_file_t* file);
skygw_file_t* skygw_file_init(char* fname, char* symlinkname);
void skygw_file_close(skygw_file_t* file, bool shutdown);
int skygw_file_write(
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

EXTERN_C_BLOCK_BEGIN

size_t get_decimal_len(size_t s);

char* replace_literal(char* haystack, 
                      const char* needle, 
                      const char* replacement);
bool is_valid_posix_path(char* path);
bool strip_escape_chars(char*);
int simple_str_hash(char* key);


EXTERN_C_BLOCK_END

#endif /* SKYGW_UTILS_H */
