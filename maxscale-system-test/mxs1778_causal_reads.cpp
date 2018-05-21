/**
 * MXS-1778: Use GTID from OK packets for consistent reads
 *
 * https://jira.mariadb.org/browse/MXS-1778
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    printf("THIS TEST IS DISABLED UNTIL A SERVER VERSION WITH THE FIX IS RELEASED\n");
    return 0;

    TestConnections test(argc, argv);
    const int N_QUERIES = 100;

    test.maxscales->connect();

    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE OR REPLACE TABLE test.t1(id INT)");

    for (int i = 0; i < N_QUERIES; i++)
    {
        std::string value = std::to_string(i);
        std::string insert = "INSERT INTO test.t1 VALUES (" + value + ")";
        std::string select = "SELECT COUNT(*) FROM test.t1 WHERE id = " + value;

        test.try_query(test.maxscales->conn_rwsplit[0], "%s", insert.c_str());
        Row row = get_row(test.maxscales->conn_rwsplit[0], select);
        test.assert(!row.empty() && row[0] == "1", "At %d: Row is %s", i, row.empty() ? "empty" : row[0].c_str());
    }

    test.try_query(test.maxscales->conn_rwsplit[0], "DROP TABLE test.t1");

    test.maxscales->disconnect();

    return test.global_result;
}
