/**
 * MXS-1873: Large session commands cause errors
 *
 * https://jira.mariadb.org/browse/MXS-1873
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.maxscale->connect();
    test.try_query(test.maxscale->conn_rwsplit[0],
                   "SET STATEMENT max_statement_time=30 FOR SELECT seq FROM seq_0_to_100000");
    test.try_query(test.maxscale->conn_rwsplit[0], "SELECT 1");
    test.maxscale->disconnect();

    return test.global_result;
}
