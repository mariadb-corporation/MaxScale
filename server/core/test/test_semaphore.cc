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

#include <maxscale/ccdefs.hh>
#include <time.h>
#include <iostream>
#include <pthread.h>
#include <signal.h>
#include <maxscale/semaphore.hh>

using namespace maxscale;
using namespace std;

namespace
{

void test_simple()
{
    bool rv;
    Semaphore sem1(1);

    cout << "Waiting for semaphore with a count of 1." << endl;
    rv = sem1.wait();
    ss_dassert(rv);
    cout << "Waited" << endl;

    Semaphore sem2(3);

    cout << "Waiting 3 times for semaphore with a count of 3." << endl;
    rv = sem2.wait();
    ss_dassert(rv);
    rv = sem2.wait();
    ss_dassert(rv);
    rv = sem2.wait();
    ss_dassert(rv);
    cout << "Waited" << endl;

    sem2.post();
    sem2.post();
    sem2.post();

    cout << "Waiting 3 times for semaphore with a count of 3." << endl;
    rv = sem2.wait();
    ss_dassert(rv);
    rv = sem2.wait();
    ss_dassert(rv);
    rv = sem2.wait();
    ss_dassert(rv);
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
    ss_dassert(!rv);
    ss_dassert((diff >= 2) && (diff <= 4));
    cout << "Waited." << endl;

    cout << "Waiting 1 second for semaphore with a count of 0..." << endl;
    started = time(NULL);
    rv = sem3.timedwait(0, 999999999);
    finished = time(NULL);
    diff = finished - started;
    ss_dassert(!rv);
    ss_dassert((diff >= 0) && (diff <= 2));
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
    pthread_t threads[n_threads];

    Semaphore sem;

    cout << "Starting threads." << endl;

    for (int i = 0; i < n_threads; ++i)
    {
        int rc = pthread_create(&threads[i], NULL, thread_main, &sem);
        ss_dassert(rc == 0);
    }

    cout << "Waiting for threads." << endl;

    sem.wait_n(n_threads);

    cout << "Joining threads." << endl;

    for (int i = 0; i < n_threads; ++i)
    {
        pthread_join(threads[i], NULL);
    }

    cout << "Joined." << endl;
}

void* send_signal(void*)
{
    cout << "Sleeping 2 seconds." << endl;
    sleep(2);

    cout << "Sending signal" << endl;
    kill(getpid(), SIGTERM);
    cout << "Sent signal" << endl;

    return NULL;
}

void sighandler(int s)
{
}

void test_signal()
{
    Semaphore sem;

    signal(SIGTERM, sighandler);

    pthread_t thread;
    int rc;

    rc = pthread_create(&thread, NULL, send_signal, NULL);
    ss_dassert(rc == 0);

    bool waited;

    cout << "Waiting" << endl;
    waited = sem.timedwait(4, Semaphore::HONOUR_SIGNALS);
    cout << "Waited" << endl;

    // Should return false and errno should be EINTR.
    ss_dassert(!waited && (errno == EINTR));

    pthread_join(thread, NULL);

    rc = pthread_create(&thread, NULL, send_signal, NULL);
    ss_dassert(rc == 0);

    cout << "Waiting" << endl;
    waited = sem.timedwait(4, Semaphore::IGNORE_SIGNALS);
    cout << "Waited" << endl;

    // Should return false and errno should be ETIMEDOUT.
    ss_dassert(!waited && (errno == ETIMEDOUT));

    pthread_join(thread, NULL);
}

}

int main(int argc, char* argv[])
{
    test_simple();
    test_threads();
    test_signal();

    return EXIT_SUCCESS;
}
