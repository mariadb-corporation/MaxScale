/*
 * Copyright (c) 2022 MariaDB Corporation Ab
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

#include <iostream>
#include <maxtest/maxrest.hh>
#include <maxtest/testconnections.hh>

using namespace std;

#define ENTER_TEST() do { cout << __func__ << endl; } while (false)

namespace
{

int alter_threads(MaxRest& maxrest, int nCurrent, int nDelta)
{
    int nThreads = nCurrent + nDelta;

    maxrest.alter_maxscale("threads", nThreads);

    if (nDelta < 0)
    {
        sleep(abs(nDelta) * 1 + 1);
    }

    auto& test = maxrest.test();

    test.expect((int)maxrest.show_threads().size() == nThreads, "Expected %d threads, but found %d.",
                nThreads, (int)maxrest.show_threads().size());

    return nThreads;
}

}

//
// create_service_with_dormant_worker
//
// - Create new worker at runtime
// - Decrease number of workers
// - Create service
// - Increase number of workers
//
void create_service(TestConnections& test, MaxRest& maxrest)
{
    ENTER_TEST();

    vector<MaxRest::Thread> threads;

    // Expect 4 initial threads.
    int nThreads = 4;
    threads = maxrest.show_threads();
    test.expect(threads.size() == 4, "1: Expected 4 initial threads, but found %d.", (int)threads.size());

    if (threads.size() != 4)
    {
        // But tune if necessary to make the rest of the test meaningful.
        nThreads = alter_threads(maxrest, threads.size(), 4 - threads.size());
    }

    nThreads = alter_threads(maxrest, nThreads, 1);
    nThreads = alter_threads(maxrest, nThreads, -1);

    // Create server, service and listener
    vector<MaxRest::Parameter> service_parameters;
    service_parameters.emplace_back("user", "maxskysql");
    service_parameters.emplace_back("password", "skysql");
    service_parameters.emplace_back("servers", "server1,server5");

    cout << "Creating server" << endl;
    maxrest.create_server("server5", "127.0.0.1", 4711);

    cout << "Creating service" << endl;
    nThreads = alter_threads(maxrest, nThreads, 1);
    maxrest.create_service("RT", "readwritesplit", service_parameters);

    cout << "Creating listener" << endl;
    nThreads = alter_threads(maxrest, nThreads, -1);
    maxrest.create_listener("RT", "RT-Listener", 5000);

    // Cleanup
    cout << "Destroying listener" << endl;
    nThreads = alter_threads(maxrest, nThreads, 1);
    maxrest.destroy_listener("RT-Listener");

    cout << "Destroying service" << endl;
    nThreads = alter_threads(maxrest, nThreads, -1);
    maxrest.destroy_service("RT", true);

    cout << "Destroying server" << endl;
    nThreads = alter_threads(maxrest, nThreads, 1);
    maxrest.destroy_server("server5");
}


void test_main(TestConnections& test)
{
    MaxRest maxrest(&test);

    try
    {
        create_service(test, maxrest);

        maxrest.alter_maxscale("threads", 4);
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
