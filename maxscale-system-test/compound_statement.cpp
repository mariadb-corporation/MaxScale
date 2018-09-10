/**
 * Test 10.1 compound statements
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    const char* sql
        = "BEGIN NOT ATOMIC\n"
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

    test.maxscales->connect();
    test.try_query(test.maxscales->conn_rwsplit[0], "DROP TABLE IF EXISTS test.t1");
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE TABLE test.t1(id INT)");
    test.try_query(test.maxscales->conn_rwsplit[0], "%s", sql);

    // Do the select inside a transacttion so that it gets routed to the master
    test.try_query(test.maxscales->conn_rwsplit[0], "BEGIN");
    test.expect(execute_query_check_one(test.maxscales->conn_rwsplit[0], "SELECT id FROM test.t1", "2") == 0,
                "Table should contain one row with value 2");
    test.try_query(test.maxscales->conn_rwsplit[0], "COMMIT");

    test.try_query(test.maxscales->conn_rwsplit[0], "DROP TABLE test.t1");
    test.maxscales->disconnect();

    test.check_maxscale_alive();
    return test.global_result;
}
