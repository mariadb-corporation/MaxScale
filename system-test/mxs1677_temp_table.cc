/**
 * MXS-1677: Error messages logged for non-text queries after temporary table is created
 *
 * https://jira.mariadb.org/browse/MXS-1677
 */
#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.maxscale->connect();
    test.try_query(test.maxscale->conn_rwsplit, "CREATE TEMPORARY TABLE test.temp(id INT)");
    test.maxscale->disconnect();

    test.log_excludes("The provided buffer does not contain a COM_QUERY, but a COM_QUIT");
    return test.global_result;
}
