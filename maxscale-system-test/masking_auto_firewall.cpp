/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include "testconnections.h"

using namespace std;

namespace
{

void init(TestConnections& test)
{
    MYSQL* pMysql = test.maxscales->conn_rwsplit[0];

    test.try_query(pMysql, "DROP TABLE IF EXISTS masking_auto_firewall");
    test.try_query(pMysql, "CREATE TABLE masking_auto_firewall (a TEXT, b TEXT)");
    test.try_query(pMysql, "INSERT INTO masking_auto_firewall VALUES ('hello', 'world')");
}

enum class Expect
{
    FAILURE,
    SUCCESS
};

void test_one(TestConnections& test, const char* zQuery, Expect expect)
{
    MYSQL* pMysql = test.maxscales->conn_rwsplit[0];

    const char* zExpect = (expect == Expect::SUCCESS ? "SHOULD" : "should NOT");

    test.tprintf("Executing '%s', %s succeed.", zQuery, zExpect);
    int rv = execute_query_silent(pMysql, zQuery);

    if (expect == Expect::SUCCESS)
    {
        test.add_result(rv, "Could NOT execute query '%s'.", zQuery);
    }
    else
    {
        test.add_result(rv == 0, "COULD execute query '%s'.", zQuery);
    }
}

void test_one_ps(TestConnections& test, const char* zQuery, Expect expect)
{
    MYSQL* pMysql = test.maxscales->conn_rwsplit[0];

    MYSQL_STMT* pPs = mysql_stmt_init(pMysql);
    int rv = mysql_stmt_prepare(pPs, zQuery, strlen(zQuery));

    if (expect == Expect::SUCCESS)
    {
        test.add_result(rv, "Could NOT prepare statement.");
    }
    else
    {
        test.add_result(rv == 0, "COULD prepare statement.");
    }

    mysql_stmt_close(pPs);
}

void run(TestConnections& test)
{
    init(test);

    MYSQL* pMysql = test.maxscales->conn_rwsplit[0];

    int rv;

    // This SHOULD go through, a is simply masked.
    test_one(test, "SELECT a, b FROM masking_auto_firewall", Expect::SUCCESS);

    // This should NOT go through as a function is used with a masked column.
    test_one(test, "SELECT LENGTH(a), b FROM masking_auto_firewall", Expect::FAILURE);

    // This SHOULD go through as a function is NOT used with a masked column
    // in a prepared statement.
    test_one(test, "PREPARE ps1 FROM 'SELECT a, LENGTH(b) FROM masking_auto_firewall'", Expect::SUCCESS);

    // This should NOT go through as a function is used with a masked column
    // in a prepared statement.
    test_one(test, "PREPARE ps2 FROM 'SELECT LENGTH(a), b FROM masking_auto_firewall'", Expect::FAILURE);

    rv = execute_query_silent(pMysql, "set @a = 'SELECT LENGTH(a), b FROM masking_auto_firewall'");
    test.add_result(rv, "Could NOT set variable.");
    // This should NOT go through as a prepared statement is prepared from a variable.
    test_one(test, "PREPARE ps3 FROM @a", Expect::FAILURE);

    // This SHOULD succeed as a function is NOT used with a masked column
    // in a binary prepared statement.
    test_one_ps(test, "SELECT a, LENGTH(b) FROM masking_auto_firewall", Expect::SUCCESS);

    // This should NOT succeed as a function is used with a masked column
    // in a binary prepared statement.
    test_one_ps(test, "SELECT LENGTH(a), b FROM masking_auto_firewall", Expect::FAILURE);
}

}

int main(int argc, char* argv[])
{
    TestConnections::skip_maxscale_start(true);

    TestConnections test(argc, argv);

    std::string json_file("/masking_auto_firewall.json");
    std::string from = test_dir + json_file;
    std::string to = "/home/vagrant" + json_file;

    if (test.maxscales->copy_to_node(0, from.c_str(), to.c_str()) == 0)
    {
        if (test.maxscales->start() == 0)
        {
            sleep(2);
            test.maxscales->wait_for_monitor();

            if (test.maxscales->connect_rwsplit() == 0)
            {
                run(test);
            }
            else
            {
                test.expect(false, "Could not connect to RWS.");
            }
        }
        else
        {
            test.expect(false, "Could not start MaxScale.");
        }
    }
    else
    {
        test.expect(false, "Could not copy masking file to MaxScale node.");
    }

    return test.global_result;
}
