/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-09-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * MXS-1507: Transaction replay stress test
 *
 * https://jira.mariadb.org/browse/MXS-1507
 */
#include <maxtest/testconnections.hh>
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>

using namespace std;

atomic<bool> running {true};

void client_thr(TestConnections& test, int id)
{
    std::string str_id = std::to_string(id);
    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Failed to connect: %s", c.error());

    while (running && test.ok())
    {
        test.expect(c.query("START TRANSACTION"),
                    "START TRANSACTION failed: %s", c.error());
        test.expect(c.query("INSERT INTO test.t1 (a) VALUES (" + str_id + ")"),
                    "INSERT failed: %s", c.error());
        auto last_id = std::to_string(c.last_insert_id());
        test.expect(c.query("UPDATE test.t1 SET a = -1 WHERE id = " + last_id),
                    "UPDATE failed: %s", c.error());
        test.expect(c.query("COMMIT"),
                    "COMMIT failed: %s", c.error());

        // The delete might fail as autocommit writes are not retried.
        // If it happens, just reconnect.
        if (!c.query("DELETE FROM test.t1 WHERE id = " + last_id))
        {
            test.expect(c.connect(), "Failed to reconnect: %s", c.error());
        }
        sleep(1);
    }
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.repl->connect();

    cout << "Creating table" << endl;
    test.try_query(test.repl->nodes[0],
                   "CREATE OR REPLACE TABLE "
                   "test.t1 (id int, a int)");

    cout << "Syncing slaves" << endl;
    test.repl->sync_slaves();


    cout << "Starting threads" << endl;
    const int N_THREADS = 1;
    vector<thread> threads;

    for (int i = 0; i < N_THREADS; i++)
    {
        threads.emplace_back(client_thr, std::ref(test), i);
    }

    for (int i = 0; i < 5; i++)
    {
        sleep(10);
        test.repl->block_node(0);
        sleep(10);
        test.repl->unblock_node(0);
    }

    cout << "Stopping threads" << endl;
    running = false;

    // Should be plenty of time for the threads to stop
    test.reset_timeout();

    for (auto& a : threads)
    {
        a.join();
    }

    test.repl->connect();
    execute_query_silent(test.repl->nodes[0], "DROP TABLE test.t1");
    execute_query_silent(test.repl->nodes[0], "DROP USER 'testuser'@'%'");
    test.repl->disconnect();

    return test.global_result;
}
