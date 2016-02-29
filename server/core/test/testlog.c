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


/** @file
    @brief (brief description)

*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <skygw_utils.h>
#include <log_manager.h>

typedef struct thread_st
{
    skygw_message_t* mes;
    simple_mutex_t*  mtx;
    size_t*          nactive;
    pthread_t        tid;
} thread_t;

static void* thr_run(void* data);
static void* thr_run_morelog(void* data);

#define MAX_NTHR 256
#define NITER 100

#if 1
#  define TEST1
#endif

#if 0
#  define TEST2
#endif

#define TEST3
#define TEST4

const char USAGE[]=
    "usage: %s [-t <#threads>]\n"
    "\n"
    "-t: Number of threads. Default is %d.\n";
const int N_THR = 4;

#define TEST_ERROR(msg)\
    do { fprintf(stderr, "[%s:%d]: %s\n", basename(__FILE__), __LINE__, msg); } while (false)

static void skygw_log_enable(int priority)
{
    mxs_log_set_priority_enabled(priority, true);
}

static void skygw_log_disable(int priority)
{
    mxs_log_set_priority_enabled(priority, false);
}

int main(int argc, char* argv[])
{
    int              err = 0;
    char*            logstr;

    int              i;
    bool             succp;
    skygw_message_t* mes;
    simple_mutex_t*  mtx;
    size_t           nactive;
    thread_t**       thr = NULL;
    time_t           t;
    struct tm        tm;
    char             c;
    int              nthr = N_THR;

    while ((c = getopt(argc, argv, "t:")) != -1)
    {
        switch (c) {
        case 't':
            nthr = atoi(optarg);
            if (nthr <= 0)
            {
                err = 1;
            }
            break;

        default:
            err = 1;
            break;
        }
    }

    if (err != 0)
    {
        fprintf(stderr, USAGE, argv[0], N_THR);
        err = 1;
        goto return_err;
    }

    printf("Using %d threads.\n", nthr);

    thr = (thread_t **)calloc(1, nthr*sizeof(thread_t*));

    if (thr == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for thread "
                "structure. Exiting.\n");
        err = 1;
        goto return_err;
    }
    i = atexit(mxs_log_finish);

    if (i != 0)
    {
        fprintf(stderr, "Couldn't register exit function.\n");
    }

    succp = mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);

    if (!succp)
    {
        fprintf(stderr, "Log manager initialization failed.\n");
    }
    ss_dassert(succp);

    t = time(NULL);
    localtime_r(&t, &tm);
    err = MXS_ERROR("%04d %02d/%02d %02d.%02d.%02d",
                    tm.tm_year+1900,
                    tm.tm_mon+1,
                    tm.tm_mday,
                    tm.tm_hour,
                    tm.tm_min,
                    tm.tm_sec);

    mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
    logstr = ("First write with flush.");
    err = MXS_ERROR("%s", logstr);

    logstr = ("Second write with flush.");
    err = MXS_ERROR("%s", logstr);

    logstr = ("Third write, no flush.");
    err = MXS_ERROR("%s", logstr);

    logstr = ("Fourth write, no flush. Next flush only.");
    err = MXS_ERROR("%s", logstr);

    err = mxs_log_flush();

    logstr = "My name is %s %d years and %d months.";
#if !defined(SS_DEBUG)
    skygw_log_enable(LOG_INFO);
#endif
    err = MXS_INFO(logstr, "TraceyTracey", 3, 7);
    mxs_log_flush();
#if !defined(SS_DEBUG)
    skygw_log_enable(LOG_INFO);
#endif
    logstr = "My name is Tracey Tracey 47 years and 7 months.";
    err = MXS_INFO("%s", logstr);
#if !defined(SS_DEBUG)
    skygw_log_enable(LOG_INFO);
#endif
    logstr = "My name is Stacey %s";
    err = MXS_INFO(logstr, "           ");
    mxs_log_finish();
#if !defined(SS_DEBUG)
    skygw_log_enable(LOG_INFO);
#endif
    logstr = "My name is Philip";
    err = MXS_INFO("%s", logstr);
#if !defined(SS_DEBUG)
    skygw_log_enable(LOG_INFO);
#endif
    logstr = "Philip.";
    err = MXS_INFO("%s", logstr);
#if !defined(SS_DEBUG)
    skygw_log_enable(LOG_INFO);
#endif
    logstr = "Ph%dlip.";
    err = MXS_INFO(logstr, 1);

    mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
    logstr = ("A terrible error has occurred!");
    err = MXS_ERROR("%s", logstr);

    logstr = ("Hi, how are you?");
    err = MXS_NOTICE("%s", logstr);

    logstr = ("I'm doing fine!");
    err = MXS_NOTICE("%s", logstr);

    logstr = ("Rather more surprising, at least at first sight, is the fact that a reference to "
              "a[i] can also be written as *(a+i). In evaluating a[i], C converts it to *(a+i) "
              "immediately; the two forms are equivalent. Applying the operators & to both parts "
              "of this equivalence, it follows that &a[i] and a+i are also identical: a+i is the "
              "address of the i-th element beyond a.");
    err = MXS_ERROR("%s", logstr);

    logstr = ("I was wondering, you know, it has been such a lovely weather whole morning and I "
              "thought that would you like to come to my place and have a little piece of cheese "
              "with us. Just me and my mom - and you, of course. Then, if you wish, we could "
              "listen to the radio and keep company for our little Steven, my mom's cat, you know.");
    err = MXS_NOTICE("%s", logstr);
    mxs_log_finish();

#if defined(TEST1)
    mes = skygw_message_init();
    mtx = simple_mutex_init(NULL, "testmtx");
    /** Test starts */

    fprintf(stderr, "\nStarting test #1 \n");

    /** 1 */
    for (i = 0; i < nthr; i++)
    {
        thr[i] = (thread_t*)calloc(1, sizeof(thread_t));
        thr[i]->mes = mes;
        thr[i]->mtx = mtx;
        thr[i]->nactive = &nactive;
    }
    nactive = nthr;

    for (i = 0; i < nthr; i++)
    {
        pthread_t p;
        pthread_create(&p, NULL, thr_run, thr[i]);
        thr[i]->tid = p;
    }

    do
    {
        skygw_message_wait(mes);
        simple_mutex_lock(mtx, true);
        if (nactive > 0)
        {
            simple_mutex_unlock(mtx);
            continue;
        }
        break;
    } while(true);

    for (i = 0; i < nthr; i++)
    {
        pthread_join(thr[i]->tid, NULL);
    }
    /** This is to release memory */
    mxs_log_finish();

    simple_mutex_unlock(mtx);

    for (i = 0; i < nthr; i++)
    {
        free(thr[i]);
    }
#endif

#if defined(TEST2)

    fprintf(stderr, "\nStarting test #2 \n");

    /** 2 */
    for (i = 0; i < nthr; i++)
    {
        thr[i] = (thread_t*)calloc(1, sizeof(thread_t));
        thr[i]->mes = mes;
        thr[i]->mtx = mtx;
        thr[i]->nactive = &nactive;
    }
    nactive = nthr;

    fprintf(stderr,
            "\nLaunching %d threads, each iterating %d times.",
            nthr,
            NITER);

    for (i = 0; i < nthr; i++)
    {
        pthread_t p;
        pthread_create(&p, NULL, thr_run_morelog, thr[i]);
        thr[i]->tid = p;
    }

    fprintf(stderr, ".. done");

    fprintf(stderr, "\nStarting to wait threads.\n");

    do
    {
        skygw_message_wait(mes);
        simple_mutex_lock(mtx, true);
        if (nactive > 0)
        {
            simple_mutex_unlock(mtx);
            continue;
        }
        break;
    } while(true);

    for (i = 0; i < nthr; i++)
    {
        pthread_join(thr[i]->tid, NULL);
    }
    /** This is to release memory */
    mxs_log_finish();

    simple_mutex_unlock(mtx);

    fprintf(stderr, "\nFreeing thread memory.");

    for (i = 0; i < nthr; i++)
    {
        free(thr[i]);
    }

    /** Test ended here */
    skygw_message_done(mes);
    simple_mutex_done(mtx);
#endif /* TEST 2 */

#if defined(TEST3)

    /**
     * Test enable/disable log.
     */
#if !defined(SS_DEBUG)
    skygw_log_enable(LOG_INFO);
#endif
    succp = mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
    ss_dassert(succp); 

    logstr = ("\tTEST 3 - test enabling and disabling logs.");
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    skygw_log_disable(LOG_INFO);

    logstr = ("1.\tWrite once to ERROR and twice to MESSAGE log.");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);
    err = MXS_INFO("%s", logstr);
    ss_dassert(err == 0);
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    skygw_log_enable(LOG_INFO);

    logstr = ("2.\tWrite to once to ERROR, twice to MESSAGE and "
              "three times to TRACE log.");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);
    err = MXS_INFO("%s", logstr);
    ss_dassert(err == 0);
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    skygw_log_disable(LOG_ERR);

    logstr = ("3.\tWrite to once to MESSAGE and twice to TRACE log.");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);
    err = MXS_INFO("%s", logstr);
    ss_dassert(err == 0);
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    skygw_log_disable(LOG_NOTICE);
    skygw_log_disable(LOG_INFO);

    logstr = ("4.\tWrite to none.");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);
    err = MXS_INFO("%s", logstr);
    ss_dassert(err == 0);
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    skygw_log_enable(LOG_ERR);
    skygw_log_enable(LOG_NOTICE);

    logstr = ("4.\tWrite once to ERROR and twice to MESSAGE log.");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);
    err = MXS_INFO("%s", logstr);
    ss_dassert(err == 0);
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    mxs_log_finish();

#endif /* TEST 3 */

#if defined(TEST4)
    succp = mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
    ss_dassert(succp);
#if !defined(SS_DEBUG)
    skygw_log_enable(LOG_INFO);
#endif
    logstr = ("\tTEST 4 - test spreading logs down to other logs.");
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    logstr = ("1.\tWrite to ERROR and thus also to MESSAGE and TRACE logs.");
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    logstr = ("2.\tWrite to MESSAGE and thus to TRACE logs.");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);

    skygw_log_enable(LOG_INFO);
    logstr = ("3.\tWrite to TRACE log only.");
    err = MXS_INFO("%s", logstr);
    ss_dassert(err == 0);

    skygw_log_disable(LOG_NOTICE);

    logstr = ("4.\tWrite to ERROR and thus also to TRACE log. "
              "MESSAGE is disabled.");
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    logstr = ("5.\tThis should not appear anywhere since MESSAGE "
              "is disabled.");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);

    mxs_log_finish();

    succp = mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
    ss_dassert(succp);
#if !defined(SS_DEBUG)
    skygw_log_enable(LOG_INFO);
#endif
    logstr = ("6.\tWrite to ERROR and thus also to MESSAGE and TRACE logs.");
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    logstr = ("7.\tWrite to MESSAGE and thus to TRACE logs.");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);

    logstr = ("8.\tWrite to TRACE log only.");
    skygw_log_enable(LOG_INFO);
    err = MXS_INFO("%s", logstr);
    ss_dassert(err == 0);

    skygw_log_disable(LOG_NOTICE);

    logstr = ("9.\tWrite to ERROR and thus also to TRACE log. "
              "MESSAGE is disabled");
    err = MXS_ERROR("%s", logstr);
    ss_dassert(err == 0);

    logstr = ("10.\tThis should not appear anywhere since MESSAGE is "
              "disabled.");
    err = MXS_NOTICE("%s", logstr);
    ss_dassert(err == 0);

    skygw_log_enable(LOG_NOTICE);

    err = MXS_ERROR("11.\tWrite to all logs some formattings : "
                    "%d %s %d",
                    (int)3,
                    "foo",
                    (int)3);
    err = MXS_ERROR("12.\tWrite to MESSAGE and TRACE log some "
                    "formattings "
                    ": %d %s %d",
                    (int)3,
                    "foo",
                    (int)3);
    err = MXS_ERROR("13.\tWrite to TRACE log some formattings "
                    ": %d %s %d",
                    (int)3,
                    "foo",
                    (int)3);

    ss_dassert(err == 0);

    mxs_log_finish();

#endif /* TEST 4 */
    fprintf(stderr, ".. done.\n");
return_err:
    if (thr != NULL)
    {
        free(thr);
    }
    return err;
}


static void* thr_run(void* data)
{
    thread_t* td = (thread_t *)data;
    char*     logstr;
    int       err;

    mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
    mxs_log_flush();
    logstr = ("Hi, how are you?");
    err = MXS_NOTICE("%s", logstr);

    if (err != 0)
    {
        TEST_ERROR("Error, log write failed.");
    }
    ss_dassert(err == 0);
    mxs_log_finish();
    mxs_log_flush();
    logstr = ("I was wondering, you know, it has been such a lovely weather whole morning and "
              "I thought that would you like to come to my place and have a little piece of "
              "cheese with us. Just me and my mom - and you, of course. Then, if you wish, "
              "we could listen to the radio and keep company for our little Steven, my mom's "
              "cat, you know.");
    if (err != 0)
    {
        TEST_ERROR("Error, log write failed.");
    }
    ss_dassert(err == 0);
    err = MXS_NOTICE("%s", logstr);
    mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
    logstr = ("Testing. One, two, three\n");
    err = MXS_ERROR("%s", logstr);
    if (err != 0)
    {
        TEST_ERROR("Error, log write failed.");
    }
    ss_dassert(err == 0);
    mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
    mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
    mxs_log_flush();
    logstr = ("For automatic and register variables, it is done each time the function or block is entered.");

#if !defined(SS_DEBUG)
    skygw_log_enable(LOG_INFO);
#endif
    err = MXS_INFO("%s", logstr);
    if (err != 0)
    {
        TEST_ERROR("Error, log write failed.");
    }
    ss_dassert(err == 0);
    mxs_log_finish();
    mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
    logstr = ("Rather more surprising, at least at first sight, is the fact that a reference "
              "to a[i] can also be written as *(a+i). In evaluating a[i], C converts it to *(a+i) "
              "immediately; the two forms are equivalent. Applying the operatos & to both parts "
              "of this equivalence, it follows that &a[i] and a+i are also identical: a+i is the "
              "address of the i-th element beyond a.");
    err = MXS_ERROR("%s", logstr);
    if (err != 0)
    {
        TEST_ERROR("Error, log write failed.");
    }
    ss_dassert(err == 0);
    mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
    mxs_log_finish();
    mxs_log_flush();
    mxs_log_finish();
    mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
    logstr = ("..and you?");
    err = MXS_NOTICE("%s", logstr);
    if (err != 0)
    {
        TEST_ERROR("Error, log write failed.");
    }
    ss_dassert(err == 0);
    mxs_log_finish();
    mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
    logstr = ("For automatic and register variables, it is done each time the function or block is entered.");
#if !defined(SS_DEBUG)
    skygw_log_enable(LOG_INFO);
#endif
    err = MXS_INFO("%s", logstr);
    if (err != 0)
    {
        TEST_ERROR("Error, log write failed.");
    }
    ss_dassert(err == 0);
    mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
    logstr = ("Rather more surprising, at least at first sight, is the fact that a reference to "
              "a[i] can also be written as *(a+i). In evaluating a[i], C converts it to *(a+i) "
              "immediately; the two forms are equivalent. Applying the operatos & to both parts "
              "of this equivalence, it follows that &a[i] and a+i are also identical: a+i is the "
              "address of the i-th element beyond a.");
    err = MXS_ERROR("%s", logstr);
    if (err != 0)
    {
        TEST_ERROR("Error, log write failed.");
    }
    ss_dassert(err == 0);
    mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
    logstr = ("..... and you too?");
    err = MXS_NOTICE("%s", logstr);
    if (err != 0)
    {
        TEST_ERROR("Error, log write failed.");
    }
    ss_dassert(err == 0);
    mxs_log_finish();
    mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
#if !defined(SS_DEBUG)
    skygw_log_enable(LOG_INFO);
#endif
    mxs_log_flush();
    logstr = ("For automatic and register variables, it is done each time the function or block is entered.");
#if !defined(SS_DEBUG)
    skygw_log_enable(LOG_INFO);
#endif
    err = MXS_INFO("%s", logstr);
    if (err != 0)
    {
        TEST_ERROR("Error, log write failed.");
    }
    ss_dassert(err == 0);
    mxs_log_finish();
    mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
    logstr = ("Testing. One, two, three, four\n");
    err = MXS_ERROR("%s", logstr);
    if (err != 0)
    {
        TEST_ERROR("Error, log write failed.");
    }
    ss_dassert(err == 0);
    mxs_log_finish();
    mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
    logstr = ("Testing. One, two, three, .. where was I?\n");
    err = MXS_ERROR("%s", logstr);
    if (err != 0)
    {
        TEST_ERROR("Error, log write failed.");
    }
    ss_dassert(err == 0);
    mxs_log_finish();
    mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
    mxs_log_finish();
    simple_mutex_lock(td->mtx, true);
    *td->nactive -= 1;
    simple_mutex_unlock(td->mtx);
    skygw_message_send(td->mes);
    return NULL;
}


static int nstr(char** str_arr)
{
    int i;

    for (i = 0; str_arr[i] != NULL; i++)
    {
    }
    return i;
}

char* logs[] = {
    "foo",
    "bar",
    "done",
    "critical test logging",
    "longer          test                   l o g g g i n g",
    "reeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"
    "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeally looooooooooooooooooooooooooooooooooooooo"
    "ooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooong line",
    "shoorter one",
    "two",
    "scrap : 834nuft984pnw8ynup4598yp8wup8upwn48t5gpn45",
    "more the same : f98uft5p8ut2p44449upnt5",
    "asdasd987987asdasd987987asdasd987987asdasd987987asdasd987987asdasd987987asdasd987987asdasd98987",
    NULL
};


static void* thr_run_morelog(void* data)
{
    thread_t* td = (thread_t *)data;
    int       err;
    int       i;
    int       nmsg;

    nmsg = nstr(logs);

    for (i = 0; i < NITER; i++)
    {
        char* str = logs[rand() % nmsg];
        err = MXS_LOG_MESSAGE((int)(rand() % (LOG_DEBUG+1)),
                              "%s - iteration # %d",
                              str,
                              i);
        if (err != 0)
        {
            fprintf(stderr, "Error, log write failed.\n");
        }
    }

    simple_mutex_lock(td->mtx, true);
    *td->nactive -= 1;
    simple_mutex_unlock(td->mtx);
    skygw_message_send(td->mes);
    return NULL;
}
