#pragma once
#ifndef _MAXSCALE_SKYGW_UTILS_H
#define _MAXSCALE_SKYGW_UTILS_H
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

#include <maxscale/cdefs.h>

MXS_BEGIN_DECLS

#define FSYNCLIMIT 10

#include <maxscale/debug.h>

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

typedef enum { THR_INIT, THR_RUNNING, THR_STOPPED, THR_DONE } skygw_thr_state_t;
typedef enum { MES_RC_FAIL, MES_RC_SUCCESS, MES_RC_TIMEOUT } skygw_mes_rc_t;


static const char* timestamp_formatstr = "%04d-%02d-%02d %02d:%02d:%02d   ";
/** One for terminating '\0' */
static const size_t timestamp_len = (4 + 1 + 2 + 1 + 2 + 1 + 2 + 1 + 2 + 1 + 2 + 3  + 1) * sizeof(char);

static const char* timestamp_formatstr_hp = "%04d-%02d-%02d %02d:%02d:%02d.%03d   ";
/** One for terminating '\0' */
static const size_t timestamp_len_hp = (4 + 1 + 2 + 1 + 2 + 1 + 2 + 1 + 2 + 1 + 2 + 1 + 3 + 3  + 1) * sizeof(
                                           char);

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

void skygw_thread_set_state(skygw_thread_t* thr,
                            skygw_thr_state_t state);
void* skygw_thread_get_data(skygw_thread_t* thr);
bool skygw_thread_must_exit(skygw_thread_t* thr);
bool skygw_thread_set_exitflag(skygw_thread_t* thr,
                               skygw_message_t* sendmes,
                               skygw_message_t* recmes);

/** Skygw thread routines */

/** Skygw file routines */
typedef enum skygw_open_mode
{
    SKYGW_OPEN_APPEND,
    SKYGW_OPEN_TRUNCATE,
} skygw_open_mode_t;

skygw_file_t* skygw_file_alloc(const char* fname);
void skygw_file_free(skygw_file_t* file);
skygw_file_t* skygw_file_init(const char* fname,
                              const char* symlinkname,
                              skygw_open_mode_t mode);
void skygw_file_close(skygw_file_t* file);
int skygw_file_write(skygw_file_t* file,
                     void*         data,
                     size_t        nbytes,
                     bool          flush);
/** Skygw file routines */

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

size_t get_decimal_len(size_t s);

MXS_END_DECLS

#endif /* SKYGW_UTILS_H */
