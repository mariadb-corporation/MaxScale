/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/semaphore.hh>
#include <maxscale/log.hh>
#include <cstdio>
#include <stdint.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <pthread.h>
#include <sched.h>
#include <thread>

using std::cerr;
using std::cout;
using std::endl;
using std::ios_base;
using std::istream;
using std::ifstream;
using std::ostream;
using std::string;
using namespace std::chrono_literals;

namespace
{

const char LOGNAME[] = "maxscale.log";
string logfile;
const size_t N_THREADS = 67;    // A nice prime number of threads

mxb::Semaphore u_semstart;
mxb::Semaphore u_semfinish;

void ensure(bool ok)
{
    if (!ok)
    {
        perror("test_logthrottling");
        exit(EXIT_FAILURE);
    }
}

ostream& operator<<(ostream& out, const MXB_LOG_THROTTLING& t)
{
    out << "{" << t.count << ", " << t.window_ms << ", " << t.suppress_ms << "}";
    return out;
}

void sleep_ms(int millis)
{
    std::this_thread::sleep_for(millis * 1ms);
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

    bool ok = (count == n_expected);
    if (ok)
    {
        cout << "Found " << count << " messages, as expected.\n";
    }
    else
    {
        cout << "###ERROR### Found " << count << " messages when " << n_expected << " was expected.\n";
    }

    return count == n_expected;
}

void log_messages(uint32_t id, size_t n_generate, int priority)
{
    for (size_t i = 0; i < n_generate; ++i)
    {
        MXB_LOG_MESSAGE(priority, "[%u] Message %lu.", id, i);

        sched_yield();
    }
}

struct THREAD_ARG
{
    uint32_t id;
    size_t   n_generate;
    int      priority;
};

void* thread_main(void* pv)
{
    THREAD_ARG* parg = static_cast<THREAD_ARG*>(pv);
    u_semstart.wait();

    log_messages(parg->id, parg->n_generate, parg->priority);

    u_semfinish.post();
    return 0;
}

bool run(const MXB_LOG_THROTTLING& throttling, int priority, size_t n_generate, size_t n_expect)
{
    cout << "Trying to log " << n_generate * N_THREADS << " messages with throttling as "
         << throttling << ".\n";

    mxb_log_set_throttling(&throttling);    // Causes message to be logged.

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

    sleep_ms(1);    // sleep 1ms, should be enough for all threads to be waiting for semaphore.

    // Let them loose.
    u_semstart.post_n(N_THREADS);
    // Wait for the results.
    u_semfinish.wait_n(N_THREADS);

    for (size_t i = 0; i < N_THREADS; ++i)
    {
        void* rv;
        pthread_join(tids[i], &rv);
    }

    return check_messages(in, n_expect);
}

bool check_continued_suppression()
{
    MXB_LOG_THROTTLING t;
    t.count = 5;
    t.window_ms = 2000;
    t.suppress_ms = 3000;

    mxb_log_reset_suppression();
    mxb_log_set_throttling(&t);

    ifstream in(logfile.c_str());
    in.seekg(0, ios_base::end);
    auto offset = in.tellg();

    cout << "Logging 100 messages, expecting 5 in the log." << endl;

    log_messages(0, 100, LOG_ERR);

    if (!check_messages(in, t.count))
    {
        return false;
    }

    in.clear();
    in.seekg(offset, ios_base::beg);

    cout << "Logging messages for 6 seconds, expecting them to continue the suppression." << endl;

    for (int i = 0; i < 6; i++)
    {
        log_messages(0, 1, LOG_ERR);
        sleep_ms(1000);
    }

    if (!check_messages(in, t.count))
    {
        return false;
    }

    in.clear();
    in.seekg(offset, ios_base::beg);

    cout << "Sleeping for 4 seconds and then logging a message." << endl;

    sleep_ms(4000);
    log_messages(0, 1, LOG_ERR);

    if (!check_messages(in, t.count + 1))
    {
        return false;
    }

    return true;
}

int main(int argc, char* argv[])
{
    int rc = 0;
    std::ios::sync_with_stdio();

    char tmpbuf[] = "/tmp/maxscale_test_logthrottling_XXXXXX";
    char* logdir = mkdtemp(tmpbuf);
    ensure(logdir);
    logfile = string(logdir) + '/' + LOGNAME;

    if (mxs_log_init(NULL, logdir, MXB_LOG_TARGET_FS))
    {
        MXB_LOG_THROTTLING t;
        t.count = 0;
        t.window_ms = 0;
        t.suppress_ms = 0;

        // No throttling, so we should get messages from all threads.
        if (!run(t, LOG_ERR, 100, N_THREADS * 100))
        {
            rc = EXIT_FAILURE;
        }

        t.count = 10;
        t.window_ms = 50;
        t.suppress_ms = 200;

        // 100 messages * N_THREADS, but due to the throttling we should get only 10 messages.
        if (!run(t, LOG_ERR, 100, 10))
        {
            rc = EXIT_FAILURE;
        }

        cout << "Sleep over suppression window.\n";
        // The sleep needs to be clearly larger than suppression window to get consistent results.
        int suppress_sleep = 600;
        sleep_ms(suppress_sleep);

        // 100 messages * N_THREADS, but due to the throttling we should get only 10 messages.
        // Since we slept longer than the suppression window, the previous message batch should
        // not affect.
        if (!run(t, LOG_ERR, 100, 10))
        {
            rc = EXIT_FAILURE;
        }

        cout << "Sleep over time window but not over suppression window. Should get no messages.\n";
        sleep_ms(100);

        // 100 messages * N_THREADS, but since we should still be within the suppression
        // window, we should get no messages.
        if (!run(t, LOG_WARNING, 100, 0))
        {
            rc = EXIT_FAILURE;
        }

        cout << "Sleep over suppression window.\n";
        sleep_ms(suppress_sleep);

        t.count = 20;
        t.window_ms = 100;
        t.suppress_ms = 500;

        // 100 messages * N_THREADS, and since we slept longer than the suppression window,
        // we should get 20 messages.
        if (!run(t, LOG_ERR, 100, 20))
        {
            rc = EXIT_FAILURE;
        }

        t.count = 10;

        // 20 messages * N_THREADS, and since we are logging NOTICE messages, we should
        // get 20 * N_THREADS messages.
        if (!run(t, LOG_NOTICE, 20, 20 * N_THREADS))
        {
            rc = EXIT_FAILURE;
        }

        mxb_log_set_priority_enabled(LOG_INFO, true);

        // 20 messages * N_THREADS, and since we are logging INFO messages, we should
        // get 20 * N_THREADS messages.
        if (!run(t, LOG_INFO, 20, 20 * N_THREADS))
        {
            rc = EXIT_FAILURE;
        }

        mxb_log_set_priority_enabled(LOG_INFO, false);

        mxb_log_set_priority_enabled(LOG_DEBUG, true);

        // 20 messages * N_THREADS, and since we are logging DEBUG messages, we should
        // get 20 * N_THREADS messages.
        if (!run(t, LOG_DEBUG, 20, 20 * N_THREADS))
        {
            rc = EXIT_FAILURE;
        }

        if (!check_continued_suppression())
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
