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

#ifndef PCRE2_CODE_UNIT_WIDTH
#define PCRE2_CODE_UNIT_WIDTH 8
#endif

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stddef.h>
#include <regex.h>
#include <maxscale/debug.h>
#include <sys/time.h>
#include "maxscale/skygw_utils.h"
#include <maxscale/atomic.h>
#include <pcre2.h>

#if !defined(PATH_MAX)
# if defined(__USE_POSIX)
#   define PATH_MAX _POSIX_PATH_MAX
# else
#   define PATH_MAX 256
# endif
#endif

static void simple_mutex_free_memory(simple_mutex_t* sm);
static void thread_free_memory(skygw_thread_t* th, char* name);
/** End of static function declarations */


int skygw_rwlock_rdlock(skygw_rwlock_t* rwlock)
{
    int err = pthread_rwlock_rdlock(rwlock->srw_rwlock);

    if (err == 0)
    {
        rwlock->srw_rwlock_thr = pthread_self();
    }
    else
    {
        rwlock->srw_rwlock_thr = 0;
        char errbuf[MXS_STRERROR_BUFLEN];
        ss_dfprintf(stderr,
                    "* pthread_rwlock_rdlock : %s\n",
                    strerror_r(err, errbuf, sizeof (errbuf)));
    }
    return err;
}

int skygw_rwlock_wrlock(skygw_rwlock_t* rwlock)
{
    int err = pthread_rwlock_wrlock(rwlock->srw_rwlock);

    if (err == 0)
    {
        rwlock->srw_rwlock_thr = pthread_self();
    }
    else
    {
        rwlock->srw_rwlock_thr = 0;
        char errbuf[MXS_STRERROR_BUFLEN];
        ss_dfprintf(stderr,
                    "* pthread_rwlock_wrlock : %s\n",
                    strerror_r(err, errbuf, sizeof (errbuf)));
    }
    return err;
}

int skygw_rwlock_unlock(skygw_rwlock_t* rwlock)
{
    int err = pthread_rwlock_rdlock(rwlock->srw_rwlock);

    if (err == 0)
    {
        rwlock->srw_rwlock_thr = 0;
    }
    else
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        ss_dfprintf(stderr, "* pthread_rwlock_unlock : %s\n",
                    strerror_r(err, errbuf, sizeof (errbuf)));
    }
    return err;
}

int skygw_rwlock_destroy(skygw_rwlock_t* rwlock)
{
    int err;
    /** Lock */
    if ((err = pthread_rwlock_wrlock(rwlock->srw_rwlock)) != 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "* Error : pthread_rwlock_wrlock failed due to %d, %s.\n",
                err, strerror_r(err, errbuf, sizeof (errbuf)));
        goto retblock;
    }
    /** Clean the struct */
    rwlock->srw_rwlock_thr = 0;
    /** Unlock */
    pthread_rwlock_unlock(rwlock->srw_rwlock);
    /** Destroy */
    if ((err = pthread_rwlock_destroy(rwlock->srw_rwlock)) != 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "* Error : pthread_rwlock_destroy failed due to %d,%s\n",
                err, strerror_r(err, errbuf, sizeof (errbuf)));
    }
    else
    {
        rwlock->srw_rwlock = NULL;
    }
retblock:
    return err;
}

int skygw_rwlock_init(skygw_rwlock_t** rwlock)
{
    skygw_rwlock_t* rwl;
    int err;

    rwl = (skygw_rwlock_t *) calloc(1, sizeof (skygw_rwlock_t));

    if (rwl == NULL)
    {
        err = 1;
        goto return_err;
    }
    rwl->srw_chk_top = CHK_NUM_RWLOCK;
    rwl->srw_chk_tail = CHK_NUM_RWLOCK;
    err = pthread_rwlock_init(rwl->srw_rwlock, NULL);
    ss_dassert(err == 0);

    if (err != 0)
    {
        free(rwl);
        char errbuf[MXS_STRERROR_BUFLEN];
        ss_dfprintf(stderr, "* Creating pthread_rwlock failed : %s\n",
                    strerror_r(err, errbuf, sizeof (errbuf)));
        goto return_err;
    }
    *rwlock = rwl;

return_err:
    return err;
}

size_t get_timestamp_len(void)
{
    return timestamp_len;
}

size_t get_timestamp_len_hp(void)
{
    return timestamp_len_hp;
}

/**
 * @node Generate and write a timestamp to location passed as argument
 * by using at most tslen characters.
 *
 * Parameters:
 * @param p_ts - in, use
 *          Write position in memory. Must be filled with at least
 *          <timestamp_len> zeroes
 *
 * @return Length of string written to p_ts. Length includes terminating '\0'.
 *
 *
 * @details (write detailed description here)
 *
 */
size_t snprint_timestamp(char* p_ts, size_t tslen)
{
    time_t t;
    struct tm tm;
    size_t rval;
    struct timeval tv;
    if (p_ts == NULL)
    {
        rval = 0;
        goto retblock;
    }

    /** Generate timestamp */

    t = time(NULL);
    localtime_r(&t, &tm);
    snprintf(p_ts, MXS_MIN(tslen, timestamp_len), timestamp_formatstr,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
             tm.tm_min, tm.tm_sec);
    rval = strlen(p_ts) * sizeof (char);
retblock:
    return rval;
}

/**
 * @node Generate and write a timestamp to location passed as argument
 * by using at most tslen characters. This will use millisecond precision.
 *
 * Parameters:
 * @param p_ts - in, use
 *          Write position in memory. Must be filled with at least
 *          <timestamp_len> zeroes
 *
 * @return Length of string written to p_ts. Length includes terminating '\0'.
 *
 *
 * @details (write detailed description here)
 *
 */
size_t snprint_timestamp_hp(char* p_ts, size_t tslen)
{
    time_t t;
    struct tm tm;
    size_t rval;
    struct timeval tv;
    int usec;
    if (p_ts == NULL)
    {
        rval = 0;
        goto retblock;
    }

    /** Generate timestamp */

    gettimeofday(&tv, NULL);
    localtime_r(&tv.tv_sec, &tm);
    usec = tv.tv_usec / 1000;
    snprintf(p_ts, MXS_MIN(tslen, timestamp_len_hp), timestamp_formatstr_hp,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, usec);
    rval = strlen(p_ts) * sizeof (char);
retblock:
    return rval;
}

/**
 * @node Initialize thread data structure
 *
 * Parameters:
 * @param name copy is taken and stored to thread structure
 *
 * @param sth_thrfun - <usage>
 *          <description>
 *
 * @param data thread data pointer
 *
 * @return thread pointer or NULL in case of failure
 *
 *
 * @details (write detailed description here)
 *
 */
skygw_thread_t* skygw_thread_init(const char* name, void* (*sth_thrfun)(void* data),
                                  void* data)
{
    skygw_thread_t* th = (skygw_thread_t *) calloc(1, sizeof (skygw_thread_t));

    if (th == NULL)
    {
        fprintf(stderr, "* Memory allocation for thread failed\n");
        goto return_th;
    }
    ss_dassert(th != NULL);
    th->sth_chk_top = CHK_NUM_THREAD;
    th->sth_chk_tail = CHK_NUM_THREAD;
    th->sth_parent = pthread_self();
    ss_debug(th->sth_state = THR_INIT);
    th->sth_name = strndup(name, PATH_MAX);
    th->sth_mutex = simple_mutex_init(NULL, name);

    if (th->sth_mutex == NULL)
    {
        thread_free_memory(th, th->sth_name);
        th = NULL;
        goto return_th;
    }
    th->sth_thrfun = sth_thrfun;
    th->sth_data = data;
    CHK_THREAD(th);

return_th:
    return th;
}

static void thread_free_memory(skygw_thread_t* th, char* name)
{
    free(name);
    free(th);
}

/**
 * @node Release skygw_thread data except filewriter.
 *
 * Parameters:
 * @param th - <usage>
 *          <description>
 *
 * @return void
 *
 *
 * @details (write detailed description here)
 *
 */
void skygw_thread_done(skygw_thread_t* th)
{
    if (th != NULL)
    {
        CHK_THREAD(th);
        ss_dassert(th->sth_state == THR_STOPPED);
        ss_debug(th->sth_state = THR_DONE);
        simple_mutex_done(th->sth_mutex);
        pthread_join(th->sth_thr, NULL);
        thread_free_memory(th, th->sth_name);
    }
}

pthread_t skygw_thread_gettid(skygw_thread_t* thr)
{
    CHK_THREAD(thr);
    return thr->sth_thr;
}

int skygw_thread_start(skygw_thread_t* thr)
{
    int err;

    CHK_THREAD(thr);
    err = pthread_create(&thr->sth_thr, NULL, thr->sth_thrfun, thr);
    ss_dassert(err == 0);

    if (err != 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "* Starting file writer thread failed due error, %d, %s\n",
                err, strerror_r(errno, errbuf, sizeof (errbuf)));
        goto return_err;
    }

return_err:
    return err;
}

#if defined(SS_DEBUG)

skygw_thr_state_t skygw_thread_get_state(skygw_thread_t* thr)
{
    CHK_THREAD(thr);
    return thr->sth_state;
}
#endif

/**
 * @node Update thread state
 *
 * Parameters:
 * @param thr - <usage>
 *          <description>
 *
 * @param state - <usage>
 *          <description>
 *
 * @return void
 *
 *
 * @details Thread must check state with mutex.
 *
 */
#if defined(SS_DEBUG)

void skygw_thread_set_state(skygw_thread_t* thr, skygw_thr_state_t state)
{
    CHK_THREAD(thr);
    simple_mutex_lock(thr->sth_mutex, true);
    thr->sth_state = state;
    simple_mutex_unlock(thr->sth_mutex);
}
#endif

/**
 * @node Set exit flag for thread from other thread
 *
 * Parameters:
 * @param thr - <usage>
 *          <description>
 *
 * @return
 *
 *
 * @details This call informs thread about exit flag and waits the response.
 *
 */
bool skygw_thread_set_exitflag(skygw_thread_t* thr, skygw_message_t* sendmes,
                               skygw_message_t* recmes)
{
    bool succp = false;

    /**
     * If thread struct pointer is NULL there's running thread
     * neither.
     */
    if (thr == NULL)
    {
        succp = true;
        goto return_succp;
    }
    CHK_THREAD(thr);
    CHK_MESSAGE(sendmes);
    CHK_MESSAGE(recmes);

    simple_mutex_lock(thr->sth_mutex, true);
    succp = !thr->sth_must_exit;
    thr->sth_must_exit = true;
    simple_mutex_unlock(thr->sth_mutex);

    /** Inform thread and wait for response */
    if (succp)
    {
        skygw_message_send(sendmes);
        skygw_message_wait(recmes);
    }

    ss_dassert(thr->sth_state == THR_STOPPED);

return_succp:
    return succp;
}

void* skygw_thread_get_data(skygw_thread_t* thr)
{
    CHK_THREAD(thr);
    return thr->sth_data;
}

bool skygw_thread_must_exit(skygw_thread_t* thr)
{
    CHK_THREAD(thr);
    return thr->sth_must_exit;
}

void acquire_lock(int* l)
{
    register int misscount = 0;
    struct timespec ts1;
    ts1.tv_sec = 0;

    while (atomic_add(l, 1) != 0)
    {
        atomic_add(l, -1);
        misscount += 1;
        if (misscount > 10)
        {
            ts1.tv_nsec = misscount * 1000000;
            nanosleep(&ts1, NULL);
        }
    }
}

void release_lock(int* l)
{
    atomic_add(l, -1);
}

/**
 * @node Create a simple_mutex structure which encapsulates pthread_mutex.
 *
 * Parameters:
 * @param mutexptr if mutex is initialized within caller's memory, this is
 * the address for it. If mutex is flat, there is value, otherwise it is NULL.
 *
 * @param name name of mutex, passed argument is copied and pointer is stored
 *  to mutex struct.
 *
 * @return simple_mutex pointer or NULL in case of failure.
 *
 *
 * @details If mutex is flat, sm_enabled can be read if the memory is not freed.
 * If flat mutex exists, sm_enabled is true.
 * If mutex allocates its own memory, the pointer is NULL if mutex doesn't
 * exist.
 *
 */
simple_mutex_t* simple_mutex_init(simple_mutex_t* mutexptr, const char* name)
{
    int err;
    simple_mutex_t* sm;

    /** Copy pointer only if flat, allocate memory otherwise. */
    if (mutexptr != NULL)
    {
        sm = mutexptr;
        sm->sm_flat = true;
    }
    else
    {
        sm = (simple_mutex_t *) calloc(1, sizeof (simple_mutex_t));
    }
    ss_dassert(sm != NULL);
#if defined(SS_DEBUG)
    sm->sm_chk_top = CHK_NUM_SIMPLE_MUTEX;
    sm->sm_chk_tail = CHK_NUM_SIMPLE_MUTEX;
#endif
    sm->sm_name = strndup(name, PATH_MAX);

    /** Create pthread mutex */
    err = pthread_mutex_init(&sm->sm_mutex, NULL);

    if (err != 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "* Initializing simple mutex %s failed due error %d, %s\n",
                name, err, strerror_r(errno, errbuf, sizeof (errbuf)));
        perror("simple_mutex : ");

        /** Write zeroes if flat, free otherwise. */
        if (sm->sm_flat)
        {
            memset(sm, 0, sizeof (*sm));
        }
        else
        {
            simple_mutex_free_memory(sm);
            sm = NULL;
        }
        goto return_sm;
    }
    sm->sm_enabled = true;
    CHK_SIMPLE_MUTEX(sm);

return_sm:
    return sm;
}

int simple_mutex_done(simple_mutex_t* sm)
{
    int err = 0;

    CHK_SIMPLE_MUTEX(sm);

    if (atomic_add(&sm->sm_enabled, -1) != 1)
    {
        atomic_add(&sm->sm_enabled, 1);
    }
    err = pthread_mutex_destroy(&sm->sm_mutex);

#if defined(NOT_USED)
    if (err != 0)
    {
        perror("simple_mutex : ");
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "* Destroying simple mutex %s failed due %d, %s\n",
                sm->sm_name, err, strerror_r(errno, errbuf, sizeof (errbuf)));
        goto return_err;
    }
#endif
    simple_mutex_free_memory(sm);

#if defined(NOT_USED)
return_err:
#endif
    return err;
}

static void simple_mutex_free_memory(simple_mutex_t* sm)
{
    if (sm->sm_name != NULL)
    {
        free(sm->sm_name);
    }
    if (!sm->sm_flat)
    {
        free(sm);
    }
}

int simple_mutex_lock(simple_mutex_t* sm, bool block)
{
    int err;

    /**
     * Leaving the following serves as a reminder. It may assert
     * any given time because sm_lock_thr is not protected.
     *
     * ss_dassert(sm->sm_lock_thr != pthread_self());
     */
    if (block)
    {
        err = pthread_mutex_lock(&sm->sm_mutex);
    }
    else
    {
        err = pthread_mutex_trylock(&sm->sm_mutex);
    }

    if (err != 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "* Locking simple mutex %s failed due error, %d, %s\n",
                sm->sm_name, err,  strerror_r(errno, errbuf, sizeof (errbuf)));
        perror("simple_mutex : ");
    }
    else
    {
        /**
         * Note that these updates are not protected.
         */
        sm->sm_locked = true;
        sm->sm_lock_thr = pthread_self();
    }
    return err;
}

int simple_mutex_unlock(simple_mutex_t* sm)
{
    int err;
    /**
     * Leaving the following serves as a reminder. It may assert
     * any given time because sm_lock_thr is not protected.
     *
     * ss_dassert(sm->sm_lock_thr == pthread_self());
     */
    err = pthread_mutex_unlock(&sm->sm_mutex);

    if (err != 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "* Unlocking simple mutex %s failed due error %d, %s\n",
                sm->sm_name, err, strerror_r(errno, errbuf, sizeof (errbuf)));
        perror("simple_mutex : ");
    }
    else
    {
        /**
         * Note that these updates are not protected.
         */
        sm->sm_locked = false;
        sm->sm_lock_thr = 0;
    }
    return err;
}

skygw_message_t* skygw_message_init(void)
{
    int err;
    skygw_message_t* mes;

    mes = (skygw_message_t*) calloc(1, sizeof (skygw_message_t));

    if (mes == NULL)
    {
        err = 1;
        goto return_mes;
    }
    mes->mes_chk_top = CHK_NUM_MESSAGE;
    mes->mes_chk_tail = CHK_NUM_MESSAGE;
    err = pthread_mutex_init(&(mes->mes_mutex), NULL);

    if (err != 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "* Initializing pthread mutex failed due error %d, %s\n",
                err, strerror_r(errno, errbuf, sizeof (errbuf)));
        free(mes);
        mes = NULL;
        goto return_mes;
    }
    err = pthread_cond_init(&(mes->mes_cond), NULL);

    if (err != 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "* Initializing pthread cond var failed, due error %d, %s\n",
                err, strerror_r(errno, errbuf, sizeof (errbuf)));
        pthread_mutex_destroy(&mes->mes_mutex);
        free(mes);
        mes = NULL;
        goto return_mes;
    }
    CHK_MESSAGE(mes);
return_mes:
    return mes;
}

void skygw_message_done(skygw_message_t* mes)
{
    int err;

    /**
     * If message struct pointer is NULL there's nothing to free.
     */
    if (mes == NULL)
    {
        return;
    }
    CHK_MESSAGE(mes);
    err = pthread_cond_destroy(&(mes->mes_cond));

    if (err != 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "* Destroying cond var failed due error %d, %s\n",
                err, strerror_r(errno, errbuf, sizeof (errbuf)));
    }
    ss_dassert(err == 0);
    err = pthread_mutex_destroy(&(mes->mes_mutex));

    if (err != 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "* Destroying pthread mutex failed, due error %d, %s\n",
                err, strerror_r(errno, errbuf, sizeof (errbuf)));
    }
    ss_dassert(err == 0);
    free(mes);
}

skygw_mes_rc_t skygw_message_send(skygw_message_t* mes)
{
    int err;
    skygw_mes_rc_t rc = MES_RC_FAIL;

    CHK_MESSAGE(mes);
    err = pthread_mutex_lock(&(mes->mes_mutex));

    if (err != 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "* Locking pthread mutex failed, due to error %d, %s\n",
                err, strerror_r(errno, errbuf, sizeof (errbuf)));
        goto return_mes_rc;
    }
    mes->mes_sent = true;
    err = pthread_cond_signal(&(mes->mes_cond));

    if (err == 0)
    {
        rc = MES_RC_SUCCESS;
    }
    else
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "* Signaling pthread cond var failed, due to error %d, %s\n",
                err, strerror_r(errno, errbuf, sizeof (errbuf)));
    }
    err = pthread_mutex_unlock(&(mes->mes_mutex));

    if (err != 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "* Unlocking pthread mutex failed, due to error %d, %s\n",
                err, strerror_r(errno, errbuf, sizeof (errbuf)));
    }

return_mes_rc:
    return rc;
}

void skygw_message_wait(skygw_message_t* mes)
{
    int err;

    CHK_MESSAGE(mes);
    err = pthread_mutex_lock(&(mes->mes_mutex));

    if (err != 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "* Locking pthread mutex failed, due error %d, %s\n",
                err, strerror_r(errno, errbuf, sizeof (errbuf)));
    }
    ss_dassert(err == 0);

    while (!mes->mes_sent)
    {
        err = pthread_cond_wait(&(mes->mes_cond), &(mes->mes_mutex));

        if (err != 0)
        {
            char errbuf[MXS_STRERROR_BUFLEN];
            fprintf(stderr, "* Locking pthread cond wait failed, due error %d, %s\n",
                    err, strerror_r(errno, errbuf, sizeof (errbuf)));
        }
    }
    mes->mes_sent = false;
    err = pthread_mutex_unlock(&(mes->mes_mutex));

    if (err != 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "* Unlocking pthread mutex failed, due error %d, %s\n",
                err, strerror_r(errno, errbuf, sizeof (errbuf)));
    }
    ss_dassert(err == 0);
}

void skygw_message_reset(skygw_message_t* mes)
{
    int err;

    CHK_MESSAGE(mes);
    err = pthread_mutex_lock(&(mes->mes_mutex));

    if (err != 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "* Locking pthread mutex failed, due error %d, %s\n",
                err, strerror_r(errno, errbuf, sizeof (errbuf)));
        goto return_mes_rc;
    }
    ss_dassert(err == 0);
    mes->mes_sent = false;
    err = pthread_mutex_unlock(&(mes->mes_mutex));

    if (err != 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "* Unlocking pthread mutex failed, due error %d, %s\n",
                err, strerror_r(errno, errbuf, sizeof (errbuf)));
        goto return_mes_rc;
    }
return_mes_rc:
    ss_dassert(err == 0);
}

/**
 * Write data to a file.
 *
 * @param file          write target
 * @param data          pointer to contiguous memory buffer
 * @param nbytes        amount of bytes to be written
 * @param flush         ensure that write is permanent
 *
 * @return 0 if succeed, errno if failed.
 */
int skygw_file_write(skygw_file_t* file, void* data, size_t nbytes, bool flush)
{
    int rc;
    size_t nwritten;
    int fd;
    static int writecount;

    CHK_FILE(file);

    nwritten = fwrite(data, nbytes, 1, file->sf_file);

    if (nwritten != 1)
    {
        rc = errno;
        perror("Logfile write.\n");
        fprintf(stderr, "* Writing %ld bytes,\n%s\n to %s failed.\n",
                nbytes, (char *) data, file->sf_fname);
        goto return_rc;
    }

    writecount += 1;

    if (flush || writecount == FSYNCLIMIT)
    {
        fd = fileno(file->sf_file);
        fflush(file->sf_file);
        fsync(fd);
        writecount = 0;
    }

    rc = 0;
    CHK_FILE(file);
return_rc:
    return rc;
}

skygw_file_t* skygw_file_alloc(const char* fname)
{
    skygw_file_t* file;

    if ((file = (skygw_file_t *) calloc(1, sizeof (skygw_file_t))) == NULL)
    {
        fprintf(stderr, "* Error : Memory allocation for file %s failed.\n", fname);
        perror("File allocation failed\n");
        return NULL;
    }
    ss_dassert(file != NULL);
    file->sf_chk_top = CHK_NUM_FILE;
    file->sf_chk_tail = CHK_NUM_FILE;
    file->sf_fname = strdup(fname);
    return file;
}

skygw_file_t* skygw_file_init(const char* fname,
                              const char* symlinkname,
                              skygw_open_mode_t mode)
{
    skygw_file_t* file;

    if ((file = skygw_file_alloc(fname)) == NULL)
    {
        /** Error was reported in skygw_file_alloc */
        goto return_file;
    }

    const char* mode_string;

    switch (mode)
    {
    case SKYGW_OPEN_TRUNCATE:
        mode_string = "w";
        break;

    default:
        ss_dassert(!true);
    case SKYGW_OPEN_APPEND:
        mode_string = "a";
    };

    if ((file->sf_file = fopen(file->sf_fname, mode_string)) == NULL)
    {
        int eno = errno;
        errno = 0;
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "* Opening file %s failed due %d, %s.\n",
                file->sf_fname, eno, strerror_r(eno, errbuf, sizeof (errbuf)));
        free(file);
        file = NULL;
        goto return_file;
    }

    setvbuf(file->sf_file, NULL, _IONBF, 0);

    CHK_FILE(file);

    /**
     * Create symlink to newly created file if name was provided.
     */
    if (symlinkname != NULL)
    {
        unlink(symlinkname);
        int rc = symlink(fname, symlinkname);

        if (rc != 0)
        {
            int eno = errno;
            errno = 0;
            char errbuf[MXS_STRERROR_BUFLEN];
            fprintf(stderr, "failed to create symlink %s -> %s due %d, %s. Exiting.",
                    fname, symlinkname, eno, strerror_r(eno, errbuf, sizeof (errbuf)));
            free(file);
            file = NULL;
            goto return_file;
        }
    }

return_file:
    return file;
}

void skygw_file_free(skygw_file_t* file)
{
    if (file)
    {
        free(file->sf_fname);
        free(file);
    }
}

void skygw_file_close(skygw_file_t* file)
{
    int fd;
    int err;

    if (file != NULL)
    {
        CHK_FILE(file);

        fd = fileno(file->sf_file);
        fsync(fd);

        if ((err = fclose(file->sf_file)) != 0)
        {
            char errbuf[MXS_STRERROR_BUFLEN];
            fprintf(stderr, "* Closing file %s failed due to %d, %s.\n",
                    file->sf_fname, errno, strerror_r(errno, errbuf, sizeof (errbuf)));
        }
        else
        {
            ss_dfprintf(stderr, "Closed %s\n", file->sf_fname);
            skygw_file_free(file);
        }
    }
}

/**
 * Calculate the number of decimal numbers from a size_t value.
 *
 * @param       value   value
 *
 * @return      number of decimal numbers of which the value consists of
 *              value==123 returns 3, for example.
 * @note        Does the same as UINTLEN macro
 */
size_t get_decimal_len(
    size_t value)
{
    return value > 0 ? (size_t) log10((double) value) + 1 : 1;
}
