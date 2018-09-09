/**
 * MXS-1549: Optimistic transaction tests
 *
 * https://jira.mariadb.org/browse/MXS-1549
 */
#include "testconnections.h"
#include <functional>
#include <iostream>
#include <vector>

using namespace std;

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    Connection conn {test.maxscales->rwsplit()};

    auto query = [&](bool should_work, string q) {
            test.assert(conn.query(q) == should_work,
                        "Query '%s' should %s: %s",
                        q.c_str(),
                        should_work ? "work" : "fail",
                        conn.error());
        };

    auto compare = [&](bool equal, string q, string res) {
            Row row = conn.row(q);
            test.assert(!row.empty() && (row[0] == res) == equal,
                        "Values are %s: `%s` `%s`",
                        equal ? "not equal" : "equal",
                        row.empty() ? "<empty>" : row[0].c_str(),
                        res.c_str());
        };

    auto block = [&](int node) {
            return bind([&](int i) {
                            test.repl->block_node(i);
                            test.maxscales->wait_for_monitor();
                        },
                        node);
        };

    auto unblock = [&](int node) {
            return bind([&](int i) {
                            test.repl->unblock_node(i);
                            test.maxscales->wait_for_monitor();
                        },
                        node);
        };

    auto ok = [&](string q) {
            return bind(query, true, q);
        };

    auto err = [&](string q) {
            return bind(query, false, q);
        };

    auto equal = [&](string q, string res) {
            return bind(compare, true, q, res);
        };

    auto not_equal = [&](string q, string res) {
            return bind(compare, false, q, res);
        };

    conn.connect();
    conn.query("CREATE OR REPLACE TABLE test.t1(id INT)");
    conn.disconnect();

    test.repl->connect();
    std::string master_id = test.repl->get_server_id_str(0);
    std::string slave_id = test.repl->get_server_id_str(1);
    test.repl->sync_slaves();

    struct
    {
        const char*               description;
        vector<function<void ()>> steps;
    } test_cases[]
    {
        {
            "Minimal transaction works",
            {
                ok("START TRANSACTION"),
                ok("COMMIT")
            }
        },
        {
            "Read-only is routed to the slave",
            {
                ok("START TRANSACTION"),
                not_equal("SELECT @@server_id", master_id),
                ok("COMMIT")
            },
        },
        {
            "Read-write is routed to the master",
            {
                ok("START TRANSACTION"),
                ok("INSERT INTO test.t1 VALUES (1)"),
                equal("SELECT @@server_id", master_id),
                ok("COMMIT")
            }
        },
        {
            "Read-only after read-write is routed to slave",
            {
                ok("START TRANSACTION"),
                ok("INSERT INTO test.t1 VALUES (1)"),
                equal("SELECT @@server_id", master_id),
                ok("COMMIT"),
                ok("START TRANSACTION"),
                equal("SELECT @@server_id", slave_id),
                ok("COMMIT")
            }
        },
        {
            "Read-write after read-only is routed to master",
            {
                ok("START TRANSACTION"),
                equal("SELECT @@server_id", slave_id),
                ok("COMMIT"),
                ok("START TRANSACTION"),
                ok("INSERT INTO test.t1 VALUES (1)"),
                equal("SELECT @@server_id", master_id),
                ok("COMMIT")
            }
        },
        {
            "Blocking slave moves transaction to the master",
            {
                ok("START TRANSACTION"),
                ok("SELECT COUNT(*) FROM test.t1"),
                block(1),
                equal("SELECT @@server_id", master_id),
                ok("COMMIT"),
                unblock(1)
            }
        },
        {
            "Blocking master has no effect",
            {
                block(0),
                ok("START TRANSACTION"),
                equal("SELECT @@server_id", slave_id),
                ok("COMMIT"),
                unblock(0)
            }
        },
        {
            "Blocking master mid-transaction has no effect",
            {
                ok("START TRANSACTION"),
                block(0),
                equal("SELECT @@server_id", slave_id),
                ok("COMMIT"),
                unblock(0)
            }
        },
        {
            "Blocking master before commit has no effect",
            {
                ok("START TRANSACTION"),
                equal("SELECT @@server_id", slave_id),
                block(0),
                ok("COMMIT"),
                unblock(0)
            }
        },
        {
            "Conflicting results terminate connection",
            {
                ok("START TRANSACTION"),
                equal("SELECT @@server_id", slave_id),
                err("INSERT INTO test.t1 VALUES (1)"),
                err("COMMIT")
            }
        },
        {
            "Read-write works without slaves",
            {
                block(1),
                ok("START TRANSACTION"),
                ok("INSERT INTO test.t1 VALUES (1)"),
                ok("COMMIT"),
                unblock(1)
            }
        },
        {
            "Read-only works without slaves",
            {
                block(1),
                ok("START TRANSACTION"),
                equal("SELECT @@server_id", master_id),
                ok("COMMIT"),
                unblock(1)
            }
        },
    };


    for (auto& a : test_cases)
    {
        test.tprintf("%s", a.description);
        conn.connect();

        // Helps debugging to have a distict query in the log
        conn.query(string("SELECT '") + a.description + "'");

        for (auto s : a.steps)
        {
            s();
        }

        conn.disconnect();
        test.repl->sync_slaves();
    }

    // Cleanup
    conn.connect();
    conn.query("DROP TABLE test.t1");
    conn.disconnect();
    test.repl->disconnect();

    return test.global_result;
}
