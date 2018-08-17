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

#if !defined(SS_DEBUG)
#define SS_DEBUG
#endif
#if defined(NDEBUG)
#undef NDEBUG
#endif

#include <maxbase/ccdefs.hh>
#include <signal.h>
#include <ctime>
#include <iostream>
#include <thread>
#include <maxbase/semaphore.hh>

using namespace maxbase;
using namespace std;

namespace
{

void test_simple()
{
    bool rv;
    Semaphore sem1(1);

    cout << "Waiting for semaphore with a count of 1." << endl;
    rv = sem1.wait();
    mxb_assert(rv);
    cout << "Waited" << endl;

    Semaphore sem2(3);

    cout << "Waiting 3 times for semaphore with a count of 3." << endl;
    rv = sem2.wait();
    mxb_assert(rv);
    rv = sem2.wait();
    mxb_assert(rv);
    rv = sem2.wait();
    mxb_assert(rv);
    cout << "Waited" << endl;

    sem2.post();
    sem2.post();
    sem2.post();

    cout << "Waiting 3 times for semaphore with a count of 3." << endl;
    rv = sem2.wait();
    mxb_assert(rv);
    rv = sem2.wait();
    mxb_assert(rv);
    rv = sem2.wait();
    mxb_assert(rv);
    cout << "Waited" << endl;

    sem2.post();
    sem2.post();
    sem2.post();

    cout << "Waiting 3 times for semaphore with a count of 3." << endl;
    rv = sem2.wait_n(3);
    cout << "Waited" << endl;

    Semaphore sem3;

    time_t started;
    time_t finished;
    time_t diff;

    cout << "Waiting 3 seconds for semaphore with a count of 0..." << endl;
    started = time(NULL);
    rv = sem3.timedwait(4);
    finished = time(NULL);
    diff = finished - started;
    mxb_assert(!rv);
    mxb_assert((diff >= 2) && (diff <= 4));
    cout << "Waited." << endl;

    cout << "Waiting 1 second for semaphore with a count of 0..." << endl;
    started = time(NULL);
    rv = sem3.timedwait(0, 999999999);
    finished = time(NULL);
    diff = finished - started;
    mxb_assert(!rv);
    mxb_assert((diff >= 0) && (diff <= 2));
    cout << "Waited." << endl;
}

void* thread_main(void* pArg)
{
    Semaphore* pSem = static_cast<Semaphore*>(pArg);

    cout << "Hello from thread" << endl;
    sleep(1);

    pSem->post();
    return 0;
}

void test_threads()
{
    const int n_threads = 10;
    std::thread threads[n_threads];

    Semaphore sem;

    cout << "Starting threads." << endl;

    for (int i = 0; i < n_threads; ++i)
    {
        threads[i] = std::thread(thread_main, &sem);
    }

    cout << "Waiting for threads." << endl;

    sem.wait_n(n_threads);

    cout << "Joining threads." << endl;

    for (int i = 0; i < n_threads; ++i)
    {
        threads[i].join();
    }

    cout << "Joined." << endl;
}

void send_signal()
{
    cout << "Sleeping 2 seconds." << endl;
    sleep(2);

    cout << "Sending signal" << endl;
    kill(getpid(), SIGTERM);
    cout << "Sent signal" << endl;
}

void sighandler(int s)
{
}

void test_signal()
{
    Semaphore sem;

    signal(SIGTERM, sighandler);

    std::thread thread(send_signal);

    bool waited;

    cout << "Waiting" << endl;
    waited = sem.timedwait(4, Semaphore::HONOUR_SIGNALS);
    cout << "Waited" << endl;

    // Should return false and errno should be EINTR.
    mxb_assert(!waited && (errno == EINTR));

    thread.join();

    thread = std::thread(send_signal);

    cout << "Waiting" << endl;
    waited = sem.timedwait(4, Semaphore::IGNORE_SIGNALS);
    cout << "Waited" << endl;

    // Should return false and errno should be ETIMEDOUT.
    mxb_assert(!waited && (errno == ETIMEDOUT));

    thread.join();
}

}

int main(int argc, char* argv[])
{
    test_simple();
    test_threads();
    test_signal();

    return EXIT_SUCCESS;
}
