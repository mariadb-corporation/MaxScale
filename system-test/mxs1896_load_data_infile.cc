/**
 * MXS-1896: LOAD DATA INFILE is mistaken for LOAD DATA LOCAL INFILE
 *
 * https://jira.mariadb.org/browse/MXS-1896
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.reset_timeout();
    test.maxscale->connect();

    test.try_query(test.maxscale->conn_rwsplit, "DROP TABLE IF EXISTS test.t1");
    test.try_query(test.maxscale->conn_rwsplit, "CREATE TABLE test.t1(id INT)");
    test.try_query(test.maxscale->conn_rwsplit, "INSERT INTO test.t1 VALUES (1), (2), (3)");
    test.try_query(test.maxscale->conn_rwsplit, "SELECT * FROM test.t1 INTO OUTFILE '/tmp/test.csv'");
    test.try_query(test.maxscale->conn_rwsplit, "LOAD DATA INFILE '/tmp/test.csv' INTO TABLE test.t1");
    test.try_query(test.maxscale->conn_rwsplit, "DROP TABLE test.t1");

    test.maxscale->disconnect();

    // Clean up the generated files
    for (int i = 0; i < 4; i++)
    {
        test.repl->ssh_node_f(i, true, "rm -f /tmp/test.csv");
    }

    return test.global_result;
}
