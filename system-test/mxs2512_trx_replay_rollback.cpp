/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
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
    SUCCESS, // Execution succeeded.
    FAILURE, // Execution failed (e.g. broken SQL).
    ERROR    // Execution succeeded but ended with an error (e.g. deadlock).
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

void execute(MYSQL* pConn, const char* zStmt, Query::Status expectation)
{
    Result rv = execute(pConn, zStmt);

    mxb_assert(rv.first == expectation);
}

}

enum class Expectation
{
    SUCCESS,
    FAILURE
};

void run_test(TestConnections& test, Expectation expectation)
{
    maxbase::Semaphore sem1;
    maxbase::Semaphore sem2;

    std::thread t1([&test, &sem1, &sem2]() {
            MYSQL* pConn = test.maxscales->open_rwsplit_connection();
            mxb_assert(pConn);
            mysql_autocommit(pConn, false);

            Query::execute(pConn, "BEGIN", Query::SUCCESS);
            Query::execute(pConn, "INSERT INTO mxs2512 VALUES(1)", Query::SUCCESS);
            sem1.post();
            sem2.wait();

            // First we sleep to be sure that t2 has time to issue its SELECT (that will block).
            sleep(5);
            Query::execute(pConn, "INSERT INTO mxs2512 VALUES(0)", Query::SUCCESS);

            mysql_close(pConn);
        });

    Query::Result rv;
    std::thread t2([&test, &sem1, &sem2, &rv]() {
            MYSQL* pConn = test.maxscales->open_rwsplit_connection();
            mxb_assert(pConn);
            mysql_autocommit(pConn, false);

            Query::execute(pConn, "BEGIN", Query::SUCCESS);
            sem1.wait();
            sem2.post();

            rv = Query::execute(pConn, "SELECT * from mxs2512 FOR UPDATE");

            mysql_close(pConn);
        });

    t1.join();
    t2.join();

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

    MYSQL* pConn = test.maxscales->open_rwsplit_connection();
    test.expect(pConn, "Could not connect to rwsplit.");

    // Preparations
    test.try_query(pConn, "DROP TABLE IF EXISTS mxs2512");
    test.try_query(pConn, "CREATE TABLE mxs2512 (x INT PRIMARY KEY)");

    // Test with 'transaction_replay=false' => should fail.
    cout << "Testing with 'transaction_replay=false', SELECT should fail." << endl;
    run_test(test, Expectation::FAILURE);

    // Intermediate cleanup; delete contents from table, turn on transaction replay, restart MaxScale.
    test.try_query(pConn, "DELETE FROM mxs2512");
    mysql_close(pConn);
    test.maxscales->stop_and_check_stopped();
    const char* zSed = "sed -i -e 's/transaction_replay=false/transaction_replay=true/' /etc/maxscale.cnf";
    test.add_result(test.maxscales->ssh_node(zSed, true), "Could not tweak /etc/maxscale.cnf");
    test.maxscales->start_and_check_started();

    // Test with 'transaction_replay=true' => should succeed.
    cout << "Testing with 'transaction_replay=true', SELECT should succeed." << endl;
    run_test(test, Expectation::SUCCESS);

    // Final cleanup
    pConn = test.maxscales->open_rwsplit_connection();
    test.expect(pConn, "Could not connect to rwsplit.");
    test.try_query(pConn, "DROP TABLE mxs2512");
    mysql_close(pConn);

    return test.global_result;
}
