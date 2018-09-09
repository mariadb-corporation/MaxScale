/**
 * Test SEQUENCE related commands
 *
 * This test is only enabled when the backend version is 10.3
 */

#include "testconnections.h"
#include <vector>

int main(int argc, char** argv)
{
    TestConnections::require_repl_version("10.3");
    TestConnections test(argc, argv);

    test.maxscales->connect();
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE SEQUENCE seq");

    std::vector<std::pair<const char*, const char*>> statements =
    {
        {"SELECT NEXT VALUE FOR seq",     "1"},
        {"SELECT PREVIOUS VALUE FOR seq", "1"},
        {"SELECT NEXTVAL(seq)",           "2"},
        {"SELECT LASTVAL(seq)",           "2"},
    };

    for (auto a : statements)
    {
        test.assert(execute_query_check_one(test.maxscales->conn_rwsplit[0], a.first, a.second) == 0,
                    "Expected '%s' for query: %s",
                    a.second,
                    a.first);
    }

    test.try_query(test.maxscales->conn_rwsplit[0], "SET SQL_MODE='ORACLE'");

    std::vector<std::pair<const char*, const char*>> oracle_statements =
    {
        {"SELECT seq.nextval", "3"},
        {"SELECT seq.currval", "3"},
    };

    for (auto a : oracle_statements)
    {
        test.assert(execute_query_check_one(test.maxscales->conn_rwsplit[0], a.first, a.second) == 0,
                    "Expected '%s' for query: %s",
                    a.second,
                    a.first);
    }

    test.try_query(test.maxscales->conn_rwsplit[0], "DROP SEQUENCE seq");
    test.maxscales->disconnect();

    return test.global_result;
}
