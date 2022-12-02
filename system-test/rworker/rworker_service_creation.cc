/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-10-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <maxtest/maxrest.hh>
#include <maxtest/testconnections.hh>

using namespace std;

#define ENTER_TEST() do { cout << __func__ << endl; } while (false)

//
// create_service_with_dormant_worker
//
// - Create new worker at runtime
// - Decrease number of workers
// - Create service
// - Increase number of workers
//
void create_service_with_dormant_worker(TestConnections& test, MaxRest& maxrest)
{
    ENTER_TEST();

    vector<MaxRest::Thread> threads;

    // Assume 4 initial threads.
    threads = maxrest.show_threads();
    test.expect(threads.size() == 4, "1: Expected 4 initial threads, but found %d.", (int)threads.size());

    // Increase to 5
    maxrest.alter_maxscale("threads", (int64_t)5);
    sleep(1);

    // Decrease back to 4
    maxrest.alter_maxscale("threads", (int64_t)4);
    sleep(1);

    // Create service and listener
    vector<MaxRest::Parameter> service_parameters;
    service_parameters.emplace_back("user", "maxskysql");
    service_parameters.emplace_back("password", "skysql");
    service_parameters.emplace_back("servers", "Server1,Server2,Server3,Server4");

    cout << "Creating service" << endl;
    maxrest.create_service("RT", "readwritesplit", service_parameters);
    cout << "Creating listener" << endl;
    maxrest.create_listener("RT", "RT-Listener", 5000);

    // Increase back to 5
    maxrest.alter_maxscale("threads", (int64_t)5);
    sleep(1);
}


void test_main(TestConnections& test)
{
    MaxRest maxrest(&test);

    try
    {
        create_service_with_dormant_worker(test, maxrest);

        maxrest.alter_maxscale("threads", (int64_t)4);
    }
    catch (const std::exception& x)
    {
        test.expect(false, "Test terminated with exception: %s", x.what());
    }
}

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}
