/**
 * MXS-1677: Error messages logged for non-text queries after temporary table is created
 *
 * https://jira.mariadb.org/browse/MXS-1677
 */
#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.maxscales->connect();
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE TEMPORARY TABLE test.temp(id INT)");
    test.maxscales->disconnect();

    test.check_log_err(0, "The provided buffer does not contain a COM_QUERY, but a COM_QUIT", false);
    return test.global_result;
}
