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

/**
 * Test KILL QUERY functionality
 */

#include <maxtest/testconnections.hh>
#include <condition_variable>

void run_one_test(TestConnections& test)
{
    auto a = test.maxscale->rwsplit();
    auto b = test.maxscale->rwsplit();
    test.expect(a.connect() && b.connect(), "Connections should work");
    auto id = a.thread_id();

    const char* query =
        "SET STATEMENT max_statement_time=60 FOR "
        "SELECT SUM(a.id) FROM t1 a JOIN t1 b JOIN t1 c JOIN t1 d WHERE a.id MOD b.id < c.id MOD d.id";

    // The query takes over 30 seconds to complete and the KILL is required to interrupt it before that.
    auto start = std::chrono::steady_clock::now();
    test.expect(a.send_query(query), "Sending the query failed: %s", a.error());
    sleep(1);
    test.expect(b.query("KILL QUERY " + std::to_string(id)), "KILL QUERY failed: %s", b.error());
    a.read_query_result();
    auto end = std::chrono::steady_clock::now();

    const char* expected = "Query execution was interrupted";
    test.expect(strstr(a.error(), expected),
                "Query should fail with '%s' but it failed with '%s'",
                expected, a.error());

    test.expect(end - start < 30s, "Query should fail in less than 30 seconds");
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Failed to connect: %s", c.error());
    test.expect(c.query("CREATE OR REPLACE TABLE t1(id INT) AS SELECT seq FROM seq_0_to_5000"),
                "CREATE failed: %s", c.error());
    test.repl->sync_slaves();

    std::vector<std::thread> threads;

    for (int i = 0; i < 50; i++)
    {
        threads.emplace_back(run_one_test, std::ref(test));
    }

    for (auto& thr : threads)
    {
        thr.join();
    }

    c.query("DROP TABLE t1");

    // MXS-4961: KILL CONNECTION_ID() returns the wrong error
    std::string errmsg = "Connection was killed";
    unsigned int errnum = 1927;     // ER_CONNECTION_KILLED
    test.expect(!c.query("KILL CONNECTION_ID()"), "Killing own connection should fail");
    test.expect(c.errnum() == errnum, "Expected error %d, got error %d", errnum, c.errnum());
    test.expect(c.error() == errmsg, "Expected message %s, got %s", errmsg.c_str(), c.error());

    return test.global_result;
}
