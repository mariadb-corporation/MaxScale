/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

#include "testconnections.h"

//
// This test makes all slaves read_only and then executes
//
//     SELECT ... FOR UPDATE
//
// first using the default system test user (that has super privileges)
// and then using a custom user that only has SELECT and UPDATE grants.
//
// Before MXS-2043, a "SELECT ... FOR UPDATE" was classified as
// QUERY_TYPE_READ, which caused the statement to be sent to a slave.
//
// With autocommit==1 and no transaction active there should be no problem
// as FOR UPDATE should have no effect unless autocommit==0 or a transaction
// is active (https://mariadb.com/kb/en/library/for-update/), but apparently
// the server checks the read_only state first and rejects the query.
//
// After MXS-2043, a "SELECT ... FOR UPDATE" statement is classified as
// QUERY_TYPE_WRITE, which unconditionally causes it to be sent to the master.
//

namespace
{

const char* ZUSER = "mxs2043_user";
const char* ZPASSWORD = "mxs2043_user";
const char* ZTABLE = "test.mxs2043";
const char* ZCOLUMN = "col";

void drop_table(TestConnections& test, MYSQL* pMysql, bool silent = false)
{
    if (!silent)
    {
        test.tprintf("Dropping table.");
    }

    test.try_query(pMysql, "DROP TABLE IF EXISTS %s", ZTABLE);
}

bool create_table(TestConnections& test, MYSQL* pMysql)
{
    test.tprintf("Creating table.");

    drop_table(test, pMysql, true);

    test.try_query(pMysql, "CREATE TABLE %s (%s INT)", ZTABLE, ZCOLUMN);

    return test.global_result == 0;
}

void drop_user(TestConnections& test, MYSQL* pMysql, bool silent = false)
{
    if (!silent)
    {
        test.tprintf("Dropping user.");
    }

    test.try_query(pMysql, "DROP USER IF EXISTS '%s'@'%%'", ZUSER);
}

bool create_user(TestConnections& test, MYSQL* pMysql)
{
    test.tprintf("Creating user.");

    drop_user(test, pMysql, true);

    test.try_query(pMysql, "CREATE USER '%s' IDENTIFIED by '%s'", ZUSER, ZPASSWORD);
    test.try_query(pMysql, "GRANT SELECT, UPDATE ON %s TO '%s'@'%%'", ZTABLE, ZUSER);

    return test.global_result == 0;
}

bool set_read_only_on_slaves(TestConnections& test, bool set)
{
    test.tprintf("%s read only on slaves.", set ? "Setting" : "Removing");

    Mariadb_nodes& ms = *test.repl;

    for (int i = 0; i < ms.N; ++i)
    {
        if (i != ms.master)
        {
            test.try_query(ms.nodes[i], "set global read_only=%d", set ? 1 : 0);
        }
    }

    return test.global_result == 0;
}

void select_for_update(TestConnections& test, MYSQL* pMysql)
{
    test.try_query(pMysql, "SELECT %s FROM %s FOR UPDATE", ZCOLUMN, ZTABLE);
}

void run_test(TestConnections& test)
{
    // The default user has super privileges, so this should succeed
    // whether or not MaxScale sends the query to the master or to
    // some slave.

    test.tprintf("Running test with default user.");
    select_for_update(test, test.maxscales->conn_rwsplit[0]);

    Maxscales& maxscales = *test.maxscales;

    MYSQL* pMysql = open_conn(maxscales.rwsplit_port[0], maxscales.IP[0],
                              ZUSER, ZPASSWORD);
    test.expect(pMysql, "Could not open connections for %s.", ZUSER);

    if (pMysql)
    {
        test.tprintf("Running test with created user.");

        // The created user does not have super privileges, so this should
        // fail unless MaxScale routes the query to the master.
        select_for_update(test, pMysql);

        mysql_close(pMysql);
    }
}
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    Maxscales& maxscales = *test.maxscales;

    maxscales.connect();

    MYSQL* pMysql = maxscales.conn_rwsplit[0];

    if (create_table(test, pMysql))
    {
        if (create_user(test, pMysql))
        {
            int rv = test.repl->connect();
            test.expect(rv == 0, "Could not connect to MS.");

            if (rv == 0)
            {
                if (set_read_only_on_slaves(test, true))
                {
                    run_test(test);
                }

                set_read_only_on_slaves(test, false);
            }

            drop_user(test, pMysql);
        }

        drop_table(test, pMysql);
    }

    return test.global_result;
}
