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
 * MXS-1506: Delayed query retry
 *
 * https://jira.mariadb.org/browse/MXS-1506
 */
#include <maxtest/testconnections.hh>
#include <functional>
#include <thread>
#include <iostream>
#include <vector>

using namespace std;

struct TestCase
{
    TestCase(string desc, std::function<void()> fn)
        : description(desc)
        , main(fn)
    {
    }

    string            description;
    function<void ()> main;     // The test function
};

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    Connection c = test.maxscale->rwsplit();

    auto send = [&](string q) {
        test.expect(c.send_query(q), "Failed to send query: %s", c.error());
    };

    auto compare = [&](string res) {
        auto f = c.read_query_result_field();

        if (test.expect(f.has_value(), "Query produced no result"))
        {
            test.expect(*f == res, "Query did not produce result of '%s' but '%s'", res.c_str(), f->c_str());
        }
    };

    auto check = [&](string q, string res) {
        test.repl->sync_slaves();
        c.connect();
        send(q);
        compare(res);
        c.disconnect();
    };

    auto ok = [&]() {
        test.expect(c.read_query_result(), "Query failed");
    };

    auto err = [&]() {
        test.expect(!c.read_query_result(), "Query succeeded");
    };

    auto query = [&](string q){
        send(q);
        ok();
    };

    auto block = [&](int node = 0) {
        test.repl->block_node(node);
        test.maxscale->wait_for_monitor();
    };

    auto unblock = [&](int node = 0) {
        test.repl->unblock_node(node);
        test.maxscale->wait_for_monitor();
    };

    vector<TestCase> tests(
    {
        {
            "Normal insert",
            [&](){
                block();
                send("INSERT INTO test.t1 VALUES (1)");
                sleep(1);
                unblock();
                ok();
                check("SELECT COUNT(*) FROM test.t1 WHERE id = 1", "1");
            }
        },
        {
            "Insert with user variables",
            [&](){
                query("SET @a = 2");
                block();
                send("INSERT INTO test.t1 VALUES (@a)");
                unblock();
                ok();
                check("SELECT COUNT(*) FROM test.t1 WHERE id = 2", "1");
            }
        },
        {
            "Normal transaction",
            [&](){
                query("START TRANSACTION");
                block();
                send("INSERT INTO test.t1 VALUES (3)");
                unblock();
                err();
                check("SELECT COUNT(*) FROM test.t1 WHERE id = 3", "0");
            }
        },
        {
            "Read-only transaction",
            [&](){
                query("START TRANSACTION READ ONLY");
                block();
                send("INSERT INTO test.t1 VALUES (4)");
                unblock();
                err();
                check("SELECT COUNT(*) FROM test.t1 WHERE id = 4", "0");
            }
        },
        {
            "Insert with autocommit=0",
            [&](){
                query("SET autocommit=0");
                block();
                send("INSERT INTO test.t1 VALUES (5)");
                unblock();
                err();
                check("SELECT COUNT(*) FROM test.t1 WHERE id = 5", "0");
            }
        },
        {
            "Interrupted insert (should cause duplicate statement execution)",
            [&](){
                send("INSERT INTO test.t1 VALUES ((SELECT SLEEP(1) + 6))");
                block();
                sleep(3);
                unblock();
                ok();
                check("SELECT COUNT(*) FROM test.t1 WHERE id = 6", "2");
            }
        },
        {
            "Interrupted insert with user variable (should cause duplicate statement execution)",
            [&](){
                query("SET @b = 7");
                send("INSERT INTO test.t1 VALUES ((SELECT SLEEP(1) + @b))");
                block();
                sleep(3);
                unblock();
                ok();
                check("SELECT COUNT(*) FROM test.t1 WHERE id = 7", "2");
            }
        },
        {
            "Interrupted insert in transaction",
            [&](){
                query("START TRANSACTION");
                send("INSERT INTO test.t1 VALUES ((SELECT SLEEP(1) + 8))");
                block();
                unblock();
                err();
                check("SELECT COUNT(*) FROM test.t1 WHERE id = 8", "0");
            }
        },
        {
            "Interrupted insert in read-only transaction",
            [&](){
                query("START TRANSACTION READ ONLY");
                send("INSERT INTO test.t1 VALUES ((SELECT SLEEP(1) + 9))");
                block();
                unblock();
                err();
                check("SELECT COUNT(*) FROM test.t1 WHERE id = 9", "0");
            }
        },
        {
            "Interrupted select",
            [&](){
                send("SELECT SLEEP(2) + 10");
                block();
                unblock();
                compare("10");
            }
        },
        {
            "Interrupted select with user variable",
            [&](){
                query("SET @c = 11");
                send("SELECT SLEEP(2) + @c");
                block();
                unblock();
                compare("11");
            }
        },
        {
            "Interrupted select in transaction",
            [&](){
                query("START TRANSACTION");
                send("SELECT SLEEP(2)");
                block();
                unblock();
                err();
            }
        },
        {
            "Interrupted select in read-only transaction",
            [&](){
                query("START TRANSACTION READ ONLY");
                send("SELECT SLEEP(2)");
                block(1);
                unblock(1);
                err();
            }
        },
        {
            "MXS-3383: Interrupted insert after session command with slow slaves (causes duplicate insert)",
            [&](){
                query("SET @b = (SELECT SLEEP(@@server_id))");
                send("INSERT INTO test.t1 VALUES ((SELECT SLEEP(1) + @b))");
                block();
                unblock();
                ok();
                check("SELECT COUNT(*) FROM test.t1", "2");
            }
        },
    });

    cout << "Create table for testing" << endl;
    c.connect();
    query("DROP TABLE IF EXISTS test.t1");
    query("CREATE TABLE test.t1 (id INT)");
    c.disconnect();

    for (auto a : tests)
    {
        if (!test.ok())
        {
            break;
        }

        test.log_printf("%s", a.description.c_str());
        c.connect();
        a.main();
        c.disconnect();

        // Remove any inserted values
        c.connect();
        query("TRUNCATE TABLE test.t1");
        c.disconnect();
    }

    c.connect();
    query("DROP TABLE test.t1");
    c.disconnect();

    return test.global_result;
}
