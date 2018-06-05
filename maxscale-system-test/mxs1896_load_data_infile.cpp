/**
 * MXS-1896: LOAD DATA INFILE is mistaken for LOAD DATA LOCAL INFILE
 *
 * https://jira.mariadb.org/browse/MXS-1896
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.set_timeout(30);
    test.maxscales->connect();

    test.try_query(test.maxscales->conn_rwsplit[0], "DROP TABLE IF EXISTS test.t1");
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE TABLE test.t1(id INT)");
    test.try_query(test.maxscales->conn_rwsplit[0], "INSERT INTO test.t1 VALUES (1), (2), (3)");
    test.try_query(test.maxscales->conn_rwsplit[0], "SELECT * FROM test.t1 INTO OUTFILE '/tmp/test.csv'");
    test.try_query(test.maxscales->conn_rwsplit[0], "LOAD DATA INFILE '/tmp/test.csv' INTO TABLE test.t1");
    test.try_query(test.maxscales->conn_rwsplit[0], "DROP TABLE test.t1");

    test.maxscales->disconnect();

    // Clean up the generated files
    for (int i = 0; i < 4; i++)
    {
        test.repl->ssh_node_f(i, true, "rm -f /tmp/test.csv");
    }

    return test.global_result;
}
