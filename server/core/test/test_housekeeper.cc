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

#include <atomic>
#include <iostream>
#include <maxbase/log.hh>
#include <maxscale/housekeeper.h>
#include <maxscale/mainworker.hh>
#include "test_utils.hh"

using namespace std;

namespace
{

std::atomic<int> n_oneshot;
std::atomic<int> n_repeating;

const char* ZONESHOT_NAME   = "OneShot";
const char* ZREPEATING_NAME = "Repeating";

bool oneshot(void*)
{
    ++n_oneshot;
    return false; // Remove from housekeeper.
}

bool repeating(void*)
{
    ++n_repeating;
    return true; // Continue calling.
}

int test()
{
    int rc = EXIT_SUCCESS;

    hktask_add(ZONESHOT_NAME, oneshot, nullptr, 1); // Call oneshot, once per second.
    hktask_add(ZREPEATING_NAME, repeating, nullptr, 1); // Call repeating, once per second.

    sleep(4); // Should get 1 oneshot call and ~4 repeating calls.

    hktask_remove(ZREPEATING_NAME);

    int n;

    n = n_oneshot.load();
    cout << "Oneshots: " << n << endl;

    if (n != 1)
    {
        cerr << "Expected 1 oneshots, got " << n << "." << endl;
        rc = EXIT_FAILURE;
    }

    n = n_repeating.load();
    cout << "Repeating: " << n << endl;

    // Let's check that the task removal really had an effect.
    sleep(2);

    if (n != n_repeating.load())
    {
        cerr << "Removed task was called." << endl;
        rc = EXIT_FAILURE;
    }

    // Timing involved, so we allow for some non-determinism.
    if (n < 3 || n > 5)
    {
        cerr << "Expected ~4 repeating, got " << n << "." << endl;
        rc = EXIT_FAILURE;
    }

    return rc;
}

}

int main(int argc, char** argv)
{
    int rc = EXIT_FAILURE;

    init_test_env();

    maxscale::MainWorker mw;
    mw.start();

    rc = test();

    mw.shutdown();
    mw.join();

    return rc;
}
