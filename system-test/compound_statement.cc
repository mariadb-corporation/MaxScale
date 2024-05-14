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
 * Test 10.1 compound statements
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    const char* sql =
        "BEGIN NOT ATOMIC\n"
        "  DECLARE EXIT HANDLER FOR SQLEXCEPTION\n"
        "  BEGIN  \n"
        "   ROLLBACK;\n"
        "   RESIGNAL;\n"
        "  END;\n"
        "  START TRANSACTION;\n"
        "    INSERT INTO test.t1 VALUES (1);\n"
        "    UPDATE test.t1 SET id = 2 WHERE id = 1;\n"
        "  COMMIT;\n"
        "END\n";

    test.maxscale->connect();
    test.try_query(test.maxscale->conn_rwsplit, "DROP TABLE IF EXISTS test.t1");
    test.try_query(test.maxscale->conn_rwsplit, "CREATE TABLE test.t1(id INT)");
    test.try_query(test.maxscale->conn_rwsplit, "%s", sql);

    // Do the select inside a transacttion so that it gets routed to the master
    test.try_query(test.maxscale->conn_rwsplit, "BEGIN");
    test.expect(execute_query_check_one(test.maxscale->conn_rwsplit, "SELECT id FROM test.t1", "2") == 0,
                "Table should contain one row with value 2");
    test.try_query(test.maxscale->conn_rwsplit, "COMMIT");

    test.try_query(test.maxscale->conn_rwsplit, "DROP TABLE test.t1");
    test.maxscale->disconnect();

    test.check_maxscale_alive();
    return test.global_result;
}
