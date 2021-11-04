/**
 * MXS-2621: Incorrect SQL if lower_case_table_names is used.
 * https://jira.mariadb.org/browse/MXS-2621
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.maxscale->connect();
    test.try_query(test.maxscale->conn_rwsplit, "SELECT 123");
    test.maxscale->disconnect();
    return test.global_result;
}
