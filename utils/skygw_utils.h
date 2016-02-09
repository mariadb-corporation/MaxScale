#ifndef _SKYGW_UTILS_H
#define _SKYGW_UTILS_H
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
 * Copyright MariaDB Corporation Ab 2016
 */

/*
 * We need a common.h file that is included by every component.
 */
#if !defined(STRERROR_BUFLEN)
#define STRERROR_BUFLEN 512
#endif

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

typedef struct skygw_file_st    skygw_file_t;
typedef struct skygw_thread_st  skygw_thread_t;
typedef struct skygw_message_st skygw_message_t;

typedef struct simple_mutex_st
{
    skygw_chk_t     sm_chk_top;
    pthread_mutex_t sm_mutex;
    pthread_t       sm_lock_thr;
    bool            sm_locked;
    int             sm_enabled; /**< defined as in to minimize mutexing */
    bool            sm_flat;
    char*           sm_name;
    skygw_chk_t     sm_chk_tail;
} simple_mutex_t;

typedef struct skygw_rwlock_st
{
    skygw_chk_t       srw_chk_top;
    pthread_rwlock_t* srw_rwlock;
    pthread_t         srw_rwlock_thr;
    skygw_chk_t       srw_chk_tail;
} skygw_rwlock_t;


typedef enum { THR_INIT, THR_RUNNING, THR_STOPPED, THR_DONE } skygw_thr_state_t;
typedef enum { MES_RC_FAIL, MES_RC_SUCCESS, MES_RC_TIMEOUT } skygw_mes_rc_t;


static const char* timestamp_formatstr = "%04d-%02d-%02d %02d:%02d:%02d   ";
/** One for terminating '\0' */
static const size_t timestamp_len = (4+1 +2+1 +2+1 +2+1 +2+1 +2+3  +1) * sizeof(char);

static const char* timestamp_formatstr_hp = "%04d-%02d-%02d %02d:%02d:%02d.%03d   ";
/** One for terminating '\0' */
static const size_t timestamp_len_hp = (4+1 +2+1 +2+1 +2+1 +2+1 +2+1+3+3  +1) * sizeof(char);

struct skygw_thread_st
{
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

struct skygw_message_st
{
    skygw_chk_t     mes_chk_top;
    bool            mes_sent;
    pthread_mutex_t mes_mutex;
    pthread_cond_t  mes_cond;
    skygw_chk_t     mes_chk_tail;
};

struct skygw_file_st
{
    skygw_chk_t  sf_chk_top;
    char*        sf_fname;
    FILE*        sf_file;
    int          sf_fd;
    skygw_chk_t  sf_chk_tail;
};

EXTERN_C_BLOCK_BEGIN

bool utils_init(); /*< Call this first before using any other function */
void utils_end();

EXTERN_C_BLOCK_END

/** Skygw thread routines */
skygw_thread_t*   skygw_thread_init(const char* name,
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

void skygw_thread_set_state(skygw_thread_t* thr,
                            skygw_thr_state_t state);
void* skygw_thread_get_data(skygw_thread_t* thr);
bool skygw_thread_must_exit(skygw_thread_t* thr);
bool skygw_thread_set_exitflag(skygw_thread_t* thr,
                               skygw_message_t* sendmes,
                               skygw_message_t* recmes);

EXTERN_C_BLOCK_END

/** Skygw thread routines */

/** Skygw file routines */
skygw_file_t* skygw_file_alloc(char* fname);
void skygw_file_free(skygw_file_t* file);
skygw_file_t* skygw_file_init(char* fname, char* symlinkname);
void skygw_file_close(skygw_file_t* file, bool shutdown);
int skygw_file_write(skygw_file_t* file,
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
void skygw_message_done(skygw_message_t* mes);
skygw_mes_rc_t skygw_message_send(skygw_message_t* mes);
void skygw_message_wait(skygw_message_t* mes);
skygw_mes_rc_t skygw_message_request(skygw_message_t* mes);
void skygw_message_reset(skygw_message_t* mes);

/** Skygw message routines */

EXTERN_C_BLOCK_END

int skygw_rwlock_wrlock(skygw_rwlock_t* rwlock);
int skygw_rwlock_rdlock(skygw_rwlock_t* rwlock);
int skygw_rwlock_unlock(skygw_rwlock_t* rwlock);
int skygw_rwlock_init(skygw_rwlock_t** rwlock);

EXTERN_C_BLOCK_BEGIN

size_t get_decimal_len(size_t s);

char* remove_mysql_comments(const char** src, const size_t* srcsize, char** dest,
                            size_t* destsize);
char* replace_values(const char** src, const size_t* srcsize, char** dest,
                     size_t* destsize);
char* replace_literal(char* haystack,
                      const char* needle,
                      const char* replacement);
char* replace_quoted(const char** src, const size_t* srcsize, char** dest, size_t* destsize);
bool is_valid_posix_path(char* path);
bool strip_escape_chars(char*);
int simple_str_hash(char* key);

EXTERN_C_BLOCK_END

#endif /* SKYGW_UTILS_H */
