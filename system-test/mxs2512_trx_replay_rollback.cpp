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

enum Status
{
    SUCCESS,// Execution succeeded.
    FAILURE,// Execution failed (e.g. broken SQL).
    ERROR   // Execution succeeded but ended with an error (e.g. deadlock).
};

struct Result : std::pair<Query::Status, std::string>
{
    Result(Status status = Query::FAILURE)
    {
        first = status;
    }

    Result(const std::string& message)
    {
        mxb_assert(!message.empty());
        first = Query::ERROR;
        second = message;
    }
};

Result execute(MYSQL* pConn, const char* zStmt)
{
    Result rv;

    if (mysql_query(pConn, zStmt) == 0)
    {
        MYSQL_RES* pRes = mysql_store_result(pConn);

        rv.second = mysql_error(pConn);

        if (rv.second.empty())
        {
            rv.first = Query::SUCCESS;
        }
        else
        {
            rv.first = Query::ERROR;
        }

        mysql_free_result(pRes);
    }
    else
    {
        rv.second = mysql_error(pConn);
    }

    return rv;
}

Result start_execute(MYSQL* pConn, const char* zStmt)
{
    Result rv;

    if (mysql_send_query(pConn, zStmt, strlen(zStmt)) == 0)
    {
        rv.first = Query::SUCCESS;
    }
    else
    {
        rv.first = Query::ERROR;
        rv.second = mysql_error(pConn);
    }

    return rv;
}

Result finish_execute(MYSQL* pConn)
{
    Result rv;

    if (mysql_read_query_result(pConn) == 0)
    {
        rv.first = Query::SUCCESS;

        if (auto* res = mysql_store_result(pConn))
        {
            mysql_free_result(res);
        }
        else if (mysql_errno(pConn))
        {
            rv.first = Query::ERROR;
            rv.second = mysql_error(pConn);
        }
    }
    else
    {
        rv.first = Query::ERROR;
        rv.second = mysql_error(pConn);
    }

    return rv;
}

void execute(MYSQL* pConn, const char* zStmt, Query::Status expectation)
{
    Result rv = execute(pConn, zStmt);

    mxb_assert(rv.first == expectation);
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
    mxb_assert(a && b);
    mysql_autocommit(b, false);
    mysql_autocommit(a, false);

    Query::execute(a, "BEGIN", Query::SUCCESS);
    Query::execute(b, "BEGIN", Query::SUCCESS);
    Query::execute(a, "INSERT INTO mxs2512 VALUES(1)", Query::SUCCESS);

    Query::start_execute(b, "SELECT * from mxs2512 FOR UPDATE");
    Query::wait_for_query(test.repl->get_connection(0), "%mxs2512%FOR UPDATE%");

    Query::execute(a, "INSERT INTO mxs2512 VALUES(0)", Query::SUCCESS);
    Query::execute(a, "COMMIT", Query::SUCCESS);
    Query::Result rv = Query::finish_execute(b);

    mysql_close(a);
    mysql_close(b);

    if (expectation == Expectation::FAILURE)
    {
        test.expect(rv.first == Query::ERROR, "SELECT did NOT fail.");
    }
    else
    {
        test.expect(rv.first == Query::SUCCESS, "SELECT DID fail.");
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
    test.try_query(pConn, "CREATE TABLE mxs2512 (x INT PRIMARY KEY)");

    // Test with 'transaction_replay=false' => should fail.
    cout << "Testing with 'transaction_replay=false', SELECT should fail." << endl;
    run_test(test, Expectation::FAILURE);



    // Intermediate cleanup; delete contents from table, turn on transaction replay.
    test.try_query(pConn, "DELETE FROM mxs2512");
    test.check_maxctrl("alter service RWS transaction_replay true");

    // Test with 'transaction_replay=true' => should succeed.
    cout << "Testing with 'transaction_replay=true', SELECT should succeed." << endl;
    run_test(test, Expectation::SUCCESS);

    // Final cleanup
    test.try_query(pConn, "DROP TABLE mxs2512");
    mysql_close(pConn);

    return test.global_result;
}
