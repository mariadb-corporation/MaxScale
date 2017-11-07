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

#include <stdlib.h>
#include <iostream>
#include <vector>
#include <maxscale/thread.hh>
// We want asserts in release mode as well.
#if !defined(SS_DEBUG)
#define SS_DEBUG
#endif
#include <maxscale/debug.h>
using std::cout;
using std::endl;
using std::vector;


int function(int i)
{
    return i / 2;
}

void test_basics()
{
    cout << __func__ << endl;

    mxs::packaged_task<int, int> t1;
    ss_dassert(!t1.valid());

    mxs::packaged_task<int, int> t2(function);
    ss_dassert(t2.valid());

    t1 = t2; // Move task.
    ss_dassert(t1.valid());
    ss_dassert(!t2.valid());

    mxs::future<int> f1;
    ss_dassert(!f1.valid());

    mxs::future<int> f2 = t1.get_future();
    ss_dassert(t1.valid());
    ss_dassert(f2.valid());

    f1 = f2; // Move future
    ss_dassert(f1.valid());
    ss_dassert(!f2.valid());
}

void test_running()
{
    cout << __func__ << endl;

    const int N = 10;

    vector<mxs::future<int> > results;
    vector<mxs::thread> threads;

    cout << "Starting threads" << endl;
    for (int i = 0; i < N; ++i)
    {
        cout << i << endl;
        mxs::packaged_task<int, int> task(function);
        mxs::future<int> r = task.get_future();
        int arg = i;
        mxs::thread t(task, arg);

        results.push_back(r);
        threads.push_back(t);
    }

    cout << "All threads started." << endl;
    cout << "Waiting for threads." << endl;

    for (int i = 0; i < N; ++i)
    {
        cout << i << endl;
        threads[i].join();
        int got = results[i].get();
        int expected = function(i);

        ss_dassert(got == expected);
    }
}

int main()
{
    test_basics();
    test_running();
    return EXIT_SUCCESS;
}
