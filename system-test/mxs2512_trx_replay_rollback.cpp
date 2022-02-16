/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <maxbase/assert.h>
#include <maxbase/semaphore.hh>
#include <maxtest/mariadb_func.hh>
#include <maxtest/testconnections.hh>

using namespace std;

namespace
{

namespace Query
{

bool execute(MYSQL* pConn, const char* zStmt)
{
    bool rv = false;

    if (mysql_query(pConn, zStmt) == 0)
    {
        MYSQL_RES* pRes = mysql_store_result(pConn);

        if (mysql_errno(pConn) == 0)
        {
            rv = true;
        }

        mysql_free_result(pRes);
    }

    return rv;
}

bool start_execute(MYSQL* pConn, const char* zStmt)
{
    bool rv;

    if (mysql_send_query(pConn, zStmt, strlen(zStmt)) == 0)
    {
        rv = true;
    }

    return rv;
}

bool finish_execute(MYSQL* pConn)
{
    bool rv = false;

    if (mysql_read_query_result(pConn) == 0)
    {
        rv = true;

        if (auto* res = mysql_store_result(pConn))
        {
            mysql_free_result(res);
        }
        else if (mysql_errno(pConn))
        {
            rv = false;
        }
    }

    return rv;
}

void wait_for_query(Connection c, std::string pattern)
{
    c.connect();

    while (c.field("SELECT COUNT(*) FROM information_schema.processlist "
                   "WHERE info LIKE '" + pattern + "' AND id != CONNECTION_ID()") == "0")
    {
    }
}
}

enum class Expectation
{
    SUCCESS,
    FAILURE
};

void run_test(TestConnections& test, Expectation expectation)
{
    MYSQL* a = test.maxscale->open_rwsplit_connection();
    MYSQL* b = test.maxscale->open_rwsplit_connection();
    test.expect(a && b, "Failed to create both connections.");

    test.expect(Query::execute(a, "BEGIN"), "First BEGIN failed.");
    test.expect(Query::execute(b, "BEGIN"), "Second BEGIN failed.");
    test.expect(Query::execute(a, "UPDATE mxs2512 SET data = data + 1 WHERE x = 0"),
                "First UPDATE failed.");
    test.expect(Query::execute(b, "UPDATE mxs2512 SET data = data + 1 WHERE x = 1"),
                "Second UPDATE failed.");

    Query::start_execute(a, "UPDATE mxs2512 SET data = data + 1 WHERE x = 1");
    Query::wait_for_query(test.repl->get_connection(0), "%x = 1%");

    // This will cause a deadlock error to be reported for this connection
    Query::start_execute(b, "UPDATE mxs2512 SET data = data + 1 WHERE x = 0");

    bool rv1 = Query::finish_execute(a);

    // The transaction must be committed before we read the result from the second connection to prevent the
    // replayed transaction from constantly conflicting with the open transaction.
    test.expect(Query::execute(a, "COMMIT"), "COMMIT failed.");

    bool rv2 = Query::finish_execute(b);
    test.expect(Query::execute(b, "ROLLBACK"), "ROLLBACK failed.");

    mysql_close(a);
    mysql_close(b);

    test.expect(rv1, "First UPDATE should always succeed.");

    if (expectation == Expectation::FAILURE)
    {
        test.expect(!rv2, "UPDATE did NOT fail.");
    }
    else
    {
        test.expect(rv2, "UPDATE DID fail.");
    }
}
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    MYSQL* pConn = test.maxscale->open_rwsplit_connection();
    test.expect(pConn, "Could not connect to rwsplit.");

    // Preparations
    test.try_query(pConn, "DROP TABLE IF EXISTS mxs2512");
    test.try_query(pConn, "CREATE TABLE mxs2512 (x INT PRIMARY KEY, data INT)");
    test.try_query(pConn, "INSERT INTO mxs2512 VALUES (0, 0), (1, 1)");

    // Test with 'transaction_replay=false' => should fail.
    cout << "Testing with 'transaction_replay=false', UPDATE should fail." << endl;
    run_test(test, Expectation::FAILURE);

    // Turn on transaction replay.
    test.check_maxctrl("alter service RWS transaction_replay true");

    // Test with 'transaction_replay=true' => should succeed.
    cout << "Testing with 'transaction_replay=true', UPDATE should succeed." << endl;
    run_test(test, Expectation::SUCCESS);

    // Final cleanup
    test.try_query(pConn, "DROP TABLE mxs2512");
    mysql_close(pConn);

    return test.global_result;
}
