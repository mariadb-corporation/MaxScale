/**
 * MXS-2621: Incorrect SQL if lower_case_table_names is used.
 * https://jira.mariadb.org/browse/MXS-2621
 */

#include "testconnections.h"

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.maxscales->connect();
    test.try_query(test.maxscales->conn_rwsplit[0], "SELECT 123");
    test.maxscales->disconnect();
    return test.global_result;
}
