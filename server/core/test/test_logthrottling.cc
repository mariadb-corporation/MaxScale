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

#include <unistd.h>
#include <cstdio>
#include <stdint.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <maxscale/log_manager.h>
#include <maxscale/random.h>

using std::cerr;
using std::cout;
using std::endl;
using std::ios_base;
using std::istream;
using std::ifstream;
using std::ostream;
using std::string;

namespace
{

const char LOGNAME[] = "maxscale.log";
static string logfile;
const size_t N_THREADS = 4;

sem_t u_semstart;
sem_t u_semfinish;

void ensure(bool ok)
{
    if (!ok)
    {
        perror("test_logthrottling");
        exit(EXIT_FAILURE);
    }
}

ostream& operator << (ostream& out, const MXS_LOG_THROTTLING& t)
{
    out << "{" << t.count << ", " << t.window_ms << ", " << t.suppress_ms << "}";
    return out;
}

}

bool check_messages(istream& in, size_t n_expected)
{
    string line;

    size_t count = 0;

    while (std::getline(in, line))
    {
        ++count;
    }

    cout << "Status: expected " << n_expected << " messages, found " << count << "." << endl;

    return count == n_expected;
}

void log_messages(uint32_t id, size_t n_generate, int priority)
{
    for (size_t i = 0; i < n_generate; ++i)
    {
        MXS_LOG_MESSAGE(priority, "[%u] Message %lu.", id, i);

        sched_yield();
    }
}

struct THREAD_ARG
{
    uint32_t id;
    size_t n_generate;
    int priority;
};

void* thread_main(void* pv)
{
    THREAD_ARG *parg = static_cast<THREAD_ARG*>(pv);

    sem_wait(&u_semstart);

    log_messages(parg->id, parg->n_generate, parg->priority);

    sem_post(&u_semfinish);
    return 0;
}

bool run(const MXS_LOG_THROTTLING& throttling, int priority, size_t n_generate, size_t n_expect)
{
    cout << "Logging " << n_generate << " messages with throttling as " << throttling << "," << endl;

    mxs_log_set_throttling(&throttling); // Causes message to be logged.

    ifstream in(logfile.c_str());
    in.seekg(0, ios_base::end);

    THREAD_ARG args[N_THREADS];
    pthread_t tids[N_THREADS];

    // Create the threads.
    for (size_t i = 0; i < N_THREADS; ++i)
    {
        THREAD_ARG* parg = &args[i];
        parg->id = i;
        parg->n_generate = n_generate;
        parg->priority = priority;

        int rc = pthread_create(&tids[i], 0, thread_main, parg);
        ensure(rc == 0);
    }

    sleep(1);

    // Let them loose.
    for (size_t i = 0; i < N_THREADS; ++i)
    {
        int rc = sem_post(&u_semstart);
        ensure(rc == 0);
    }

    // Wait for the results.
    for (size_t i = 0; i < N_THREADS; ++i)
    {
        int rc = sem_wait(&u_semfinish);
        ensure(rc == 0);
    }

    for (size_t i = 0; i < N_THREADS; ++i)
    {
        void* rv;
        pthread_join(tids[i], &rv);
    }

    return check_messages(in, n_expect);
}

int main(int argc, char* argv[])
{
    int rc;

    std::ios::sync_with_stdio();
    rc = sem_init(&u_semstart, 0, 0);
    ensure(rc == 0);

    rc = sem_init(&u_semfinish, 0, 0);
    ensure(rc == 0);


    char tmpbuf[] = "/tmp/maxscale_test_logthrottling_XXXXXX";
    char* logdir = mkdtemp(tmpbuf);
    ensure(logdir);
    logfile.assign(string(logdir) + '/' + LOGNAME);

    if (mxs_log_init(NULL, logdir, MXS_LOG_TARGET_FS))
    {
        MXS_LOG_THROTTLING t;

        t.count = 0;
        t.window_ms = 0;
        t.suppress_ms = 0;

        // No throttling, so we should get messages from all threads.
        if (!run(t, LOG_ERR, 100, N_THREADS * 100))
        {
            rc = EXIT_FAILURE;
        }

        t.count = 10;
        t.window_ms = 2000;
        t.suppress_ms = 5000;

        // 100 messages * N_THREADS, but due to the throttling we should get only 10 messages.
        if (!run(t, LOG_ERR, 100, 10))
        {
            rc = EXIT_FAILURE;
        }

        cout << "Sleeping 7 seconds." << endl;
        sleep(7);

        // 100 messages * N_THREADS, but due to the throttling we should get only 10 messages.
        // Since we slept longer than the suppression window, the previous message batch should
        // not affect.
        if (!run(t, LOG_ERR, 100, 10))
        {
            rc = EXIT_FAILURE;
        }

        cout << "Sleeping 1 seconds." << endl;
        sleep(1);

        // 100 messages * N_THREADS, but since we should still be within the suppression
        // window, we should get no messages.
        if (!run(t, LOG_WARNING, 100, 0))
        {
            rc = EXIT_FAILURE;
        }

        cout << "Sleeping 6 seconds." << endl;
        sleep(6);

        t.count = 20;
        t.window_ms = 1000;
        t.suppress_ms = 5000;

        // 100 messages * N_THREADS, and since we slept longer than the suppression window,
        // we should get 20 messages.
        if (!run(t, LOG_ERR, 100, 20))
        {
            rc = EXIT_FAILURE;
        }

        t.count = 10;
        t.window_ms = 1000;
        t.suppress_ms = 5000;

        // 20 messages * N_THREADS, and since we are logging NOTICE messages, we should
        // get 20 * N_THREADS messages.
        if (!run(t, LOG_NOTICE, 20, 20 * N_THREADS))
        {
            rc = EXIT_FAILURE;
        }

        mxs_log_set_priority_enabled(LOG_INFO, true);

        // 20 messages * N_THREADS, and since we are logging INFO messages, we should
        // get 20 * N_THREADS messages.
        if (!run(t, LOG_INFO, 20, 20 * N_THREADS))
        {
            rc = EXIT_FAILURE;
        }

        mxs_log_set_priority_enabled(LOG_INFO, false);

        mxs_log_set_priority_enabled(LOG_DEBUG, true);

        // 20 messages * N_THREADS, and since we are logging DEBUG messages, we should
        // get 20 * N_THREADS messages.
        if (!run(t, LOG_DEBUG, 20, 20 * N_THREADS))
        {
            rc = EXIT_FAILURE;
        }

        mxs_log_finish();
    }
    else
    {
        rc = EXIT_FAILURE;
    }

    // A crude method to remove all files but it works
    string cmd = "rm -r ";
    cmd += logdir;
    if (system(cmd.c_str()) == -1)
    {
        cerr << "Could not remove all files";
    }

    return rc;
}
