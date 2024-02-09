/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

/**
 * MXS-1507: Transaction replay tests
 *
 * https://jira.mariadb.org/browse/MXS-1507
 */
#include <maxtest/testconnections.hh>
#include <functional>
#include <iostream>
#include <vector>

using namespace std;

const std::string BIG_VALUE(1500, 'a');

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto ok = [&](string q) {
        return [&test, q](string& name, Connection& c){
            test.expect(c.query(q), " <%s> Query '%s' should work: %s", name.c_str(), q.c_str(), c.error());
        };
    };

    auto err = [&](string q) {
        return [&test, q](string& name, Connection& c){
            test.expect(!c.query(q), "<%s> Query should not work: %s", name.c_str(), q.c_str());
        };
    };

    auto check = [&](string q, string res) {
        return [&test, q, res](string& name, Connection& c){
            auto f = c.field(q);
            test.expect(f == res,
                        "<%s> Query '%s' should return '%s' not '%s (%s)",
                        name.c_str(), q.c_str(), res.c_str(), f.c_str(), c.error());
        };
    };

    struct TrxTest
    {
        string                                       description;
        vector<function<void(string&, Connection&)>> pre;
        vector<function<void(string&, Connection&)>> post;
        vector<function<void(string&, Connection&)>> check;
    };

    std::vector<TrxTest> tests
    ({
        {
            "Basic transaction",
            {
                ok("BEGIN"),
                ok("SELECT 1"),
            },
            {
                ok("SELECT 2"),
                ok("COMMIT"),
            },
            {
            }
        },
        {
            "Large result",
            {
                ok("BEGIN"),
                ok("SELECT REPEAT('a', 100000)"),
            },
            {
                ok("SELECT REPEAT('a', 100000)"),
                ok("COMMIT"),
            },
            {
            }
        },
        {
            "Transaction with a write",
            {
                ok("CREATE OR REPLACE TABLE test.t1(id INT)"),
                ok("BEGIN"),
                ok("INSERT INTO test.t1 VALUES (1)"),
            },
            {
                ok("INSERT INTO test.t1 VALUES (2)"),
                ok("COMMIT"),
            },
            {
                check("SELECT COUNT(*) FROM test.t1 WHERE id IN (1, 2)", "2"),
                ok("DROP TABLE test.t1"),
            },
        },
        {
            "Read-only transaction",
            {
                ok("START TRANSACTION READ ONLY"),
                ok("SELECT 1"),
            },
            {
                ok("SELECT 2"),
                ok("COMMIT"),
            },
            {
            }
        },
        {
            "Trx started, no queries",
            {
                ok("BEGIN"),
            },
            {
                ok("SELECT 1"),
                ok("COMMIT"),
            },
            {
            }
        },
        {
            "Trx waiting on commit",
            {
                ok("BEGIN"),
                ok("SELECT 1"),
            },
            {
                ok("COMMIT"),
            },
            {
            }
        },
        {
            "Trx with NOW()",
            {
                ok("BEGIN"),
                ok("SELECT NOW(), SLEEP(1)"),
            },
            {
                err("SELECT 1"),
            },
            {
            }
        },
        {
            "Commit trx with NOW()",
            {
                ok("BEGIN"),
                ok("SELECT NOW(), SLEEP(1)"),
            },
            {
                err("COMMIT"),
            },
            {
            }
        },
        {
            "NOW() used after replay",
            {
                ok("BEGIN"),
                ok("SELECT 1"),
            },
            {
                ok("SELECT NOW()"),
                ok("COMMIT"),
            },
            {
            }
        },
        {
            "Exceed transaction length limit",
            {
                ok("BEGIN"),
                ok("SELECT '" + BIG_VALUE + "'"),
            },
            {
                err("SELECT 7"),
                err("COMMIT"),
            },
            {
            }
        },
        {
            "Normal trx after hitting limit",
            {
                ok("BEGIN"),
                ok("SELECT '" + BIG_VALUE + "'"),
            },
            {
                err("SELECT 8"),
                err("COMMIT"),
            },
            {
                ok("BEGIN"),
                ok("SELECT 1"),
                ok("SELECT 2"),
                ok("COMMIT"),
            }
        },
        {
            "Session command inside transaction",
            {
                ok("BEGIN"),
                ok("SET @a = 1"),
            },
            {
                check("SELECT @a", "1"),
                ok("COMMIT"),
            },
            {
            }
        },
        {
            "Empty transaction",
            {
                ok("BEGIN"),
            },
            {
                ok("COMMIT"),
            },
            {
            }
        }
    });

    std::vector<Connection> conns;

    for (auto& a : tests)
    {
        conns.emplace_back(test.maxscale->rwsplit());
        test.expect(conns.back().connect(), "Failed to connect: %s", conns.back().error());
    }

    for (size_t i = 0; i < tests.size(); i++)
    {
        for (auto& f : tests[i].pre)
        {
            f(tests[i].description, conns[i]);
        }
    }

    // Block and unblock the master
    test.repl->block_node(0);
    test.maxscale->wait_for_monitor(2);
    test.repl->unblock_node(0);
    test.maxscale->wait_for_monitor(2);

    for (size_t i = 0; i < tests.size(); i++)
    {
        for (auto& f : tests[i].post)
        {
            f(tests[i].description, conns[i]);
        }
    }

    for (size_t i = 0; i < tests.size(); i++)
    {
        conns[i].disconnect();
    }

    test.repl->connect();
    test.repl->sync_slaves();
    test.repl->disconnect();

    for (size_t i = 0; i < tests.size(); i++)
    {
        test.expect(conns[i].connect(), "Failed to reconnect: %s", conns[i].error());

        for (auto& f : tests[i].check)
        {
            f(tests[i].description, conns[i]);
        }
    }

    return test.global_result;
}
