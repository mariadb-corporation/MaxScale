/**
 * MXS-1507: Transaction replay tests
 *
 * https://jira.mariadb.org/browse/MXS-1507
 */
#include "testconnections.h"
#include <functional>
#include <iostream>
#include <vector>

using namespace std;

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto query = [&](string q) {
            return execute_query_silent(test.maxscales->conn_rwsplit[0], q.c_str()) == 0;
        };

    auto ok = [&](string q) {
            test.assert(query(q),
                        "Query '%s' should work: %s",
                        q.c_str(),
                        mysql_error(test.maxscales->conn_rwsplit[0]));
        };

    auto err = [&](string q) {
            test.assert(!query(q), "Query should not work: %s", q.c_str());
        };

    auto check = [&](string q, string res) {
            Row row = get_row(test.maxscales->conn_rwsplit[0], q.c_str());
            test.assert(!row.empty() && row[0] == res,
                        "Query '%s' should return 1: %s (%s)",
                        q.c_str(),
                        row.empty() ? "<empty>" : row[0].c_str(),
                        mysql_error(test.maxscales->conn_rwsplit[0]));
        };

    struct TrxTest
    {
        string                    description;
        vector<function<void ()>> pre;
        vector<function<void ()>> post;
        vector<function<void ()>> check;
    };

    std::vector<TrxTest> tests
    ({
        {
            "Basic transaction",
            {
                bind(ok, "BEGIN"),
                bind(ok, "SELECT 1"),
            },
            {
                bind(ok, "SELECT 2"),
                bind(ok, "COMMIT"),
            },
            {
            }
        },
        {
            "Large result",
            {
                bind(ok, "BEGIN"),
                bind(ok, "SELECT REPEAT('a', 100000)"),
            },
            {
                bind(ok, "SELECT REPEAT('a', 100000)"),
                bind(ok, "COMMIT"),
            },
            {
            }
        },
        {
            "Transaction with a write",
            {
                bind(ok, "BEGIN"),
                bind(ok, "INSERT INTO test.t1 VALUES (1)"),
            },
            {
                bind(ok, "INSERT INTO test.t1 VALUES (2)"),
                bind(ok, "COMMIT"),
            },
            {
                bind(check, "SELECT COUNT(*) FROM test.t1 WHERE id IN (1, 2)", "2"),
            }
        },
        {
            "Read-only transaction",
            {
                bind(ok, "START TRANSACTION READ ONLY"),
                bind(ok, "SELECT 1"),
            },
            {
                bind(ok, "SELECT 2"),
                bind(ok, "COMMIT"),
            },
            {
            }
        },
        {
            "Trx started, no queries",
            {
                bind(ok, "BEGIN"),
            },
            {
                bind(ok, "SELECT 1"),
                bind(ok, "COMMIT"),
            },
            {
            }
        },
        {
            "Trx waiting on commit",
            {
                bind(ok, "BEGIN"),
                bind(ok, "SELECT 1"),
            },
            {
                bind(ok, "COMMIT"),
            },
            {
            }
        },
        {
            "Trx with NOW()",
            {
                bind(ok, "BEGIN"),
                bind(ok, "SELECT NOW(), SLEEP(1)"),
            },
            {
                bind(err, "SELECT 1"),
            },
            {
            }
        },
        {
            "Commit trx with NOW()",
            {
                bind(ok, "BEGIN"),
                bind(ok, "SELECT NOW(), SLEEP(1)"),
            },
            {
                bind(err, "COMMIT"),
            },
            {
            }
        },
        {
            "NOW() used after replay",
            {
                bind(ok, "BEGIN"),
                bind(ok, "SELECT 1"),
            },
            {
                bind(ok, "SELECT NOW()"),
                bind(ok, "COMMIT"),
            },
            {
            }
        },
        {
            "Exceed transaction length limit",
            {
                bind(ok, "BEGIN"),
                bind(ok,
                     "SELECT 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'"),
            },
            {
                bind(err, "SELECT 7"),
                bind(err, "COMMIT"),
            },
            {
            }
        },
        {
            "Normal trx after hitting limit",
            {
                bind(ok, "BEGIN"),
                bind(ok,
                     "SELECT 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                     "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'"),
            },
            {
                bind(err, "SELECT 7"),
                bind(err, "COMMIT"),
            },
            {
                bind(ok, "BEGIN"),
                bind(ok, "SELECT 1"),
                bind(ok, "SELECT 2"),
                bind(ok, "COMMIT"),
            }
        },
        {
            "Session command inside transaction",
            {
                bind(ok, "BEGIN"),
                bind(ok, "SET @a = 1"),
            },
            {
                bind(check, "SELECT @a", "1"),
                bind(ok, "COMMIT"),
            },
            {
            }
        },
        {
            "Empty transaction",
            {
                bind(ok, "BEGIN"),
            },
            {
                bind(ok, "COMMIT"),
            },
            {
            }
        }
    });

    // Create a table for testing
    test.maxscales->connect();
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE OR REPLACE TABLE test.t1(id INT)");
    test.maxscales->disconnect();

    int i = 1;

    for (auto& a : tests)
    {
        test.set_timeout(90);
        test.tprintf("%d: %s", i++, a.description.c_str());

        test.maxscales->connect();
        for (auto& f : a.pre)
        {
            f();
        }

        // Block and unblock the master
        test.repl->block_node(0);
        test.maxscales->wait_for_monitor();
        test.repl->unblock_node(0);
        test.maxscales->wait_for_monitor();

        for (auto& f : a.post)
        {
            f();
        }
        test.maxscales->disconnect();

        test.repl->connect();
        test.repl->sync_slaves();
        test.repl->disconnect();

        test.maxscales->connect();
        for (auto& f : a.check)
        {
            f();
        }
        test.maxscales->disconnect();

        // Clear the table at the end of the test
        test.maxscales->connect();
        test.try_query(test.maxscales->conn_rwsplit[0], "TRUNCATE TABLE test.t1");
        test.maxscales->disconnect();
    }

    test.maxscales->connect();
    test.try_query(test.maxscales->conn_rwsplit[0], "DROP TABLE test.t1");
    test.maxscales->disconnect();

    return test.global_result;
}
