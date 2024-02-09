/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/threadpool.hh>
#include <iostream>
#include <sstream>
#include <maxbase/assert.hh>
#include <maxbase/log.hh>
#include <maxbase/semaphore.hh>
#include <maxbase/string.hh>

using namespace std;
using mxb::ThreadPool;

namespace
{

mxb::Semaphore* pSem_stop;
mxb::Semaphore* pSem_start;
ThreadPool* pTp;

void iterative_thread(int i)
{
    stringstream ss;
    pSem_start->wait();
    ss << "In thread " << i << "\n";
    cout << ss.str() << flush;
    pSem_stop->post();
}

void test_iterative(int limit)
{
    ThreadPool tp(limit);
    pTp = &tp;

    mxb::Semaphore sem_stop;
    pSem_stop = &sem_stop;

    mxb::Semaphore sem_start;
    pSem_start = &sem_start;

    for (int i = 0; i < 10; ++i)
    {
        tp.execute([i]() {
                       iterative_thread(i);
                   }, MAKE_STR("Test-" << i));
    }

    sem_start.post_n(10);

    cout << "Waiting.\n" << flush;
    sem_stop.wait_n(10);
    cout << "Waited.\n" << flush;
}

void recursive_thread(int i)
{
    if (i > 1)
    {
        pTp->execute([i]() {
                         recursive_thread(i - 1);
                     }, MAKE_STR("Recursive-" << i));
    }

    stringstream ss;
    ss << "In thread " << i << "\n";
    cout << ss.str() << flush;

    pSem_stop->post();
}

void test_recursive(int limit)
{
    ThreadPool tp(limit);
    pTp = &tp;

    mxb::Semaphore sem;
    pSem_stop = &sem;

    tp.execute([]() {
                   recursive_thread(10);
               }, "Recursive");

    cout << "Waiting.\n" << flush;
    sem.wait_n(10);
    cout << "Waited.\n" << flush;
}
}

int main()
{
    mxb::Log log;

    test_iterative(ThreadPool::UNLIMITED);
    test_iterative(1);
    test_iterative(3);

    test_recursive(ThreadPool::UNLIMITED);
    test_recursive(1);

    return 0;
}
