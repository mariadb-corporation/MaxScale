/**
 * Test SEQUENCE related commands
 *
 * This test is only enabled when the backend version is 10.3
 */

#include <maxtest/testconnections.hh>
#include <vector>

int main(int argc, char** argv)
{
    TestConnections::require_repl_version("10.3");
    TestConnections test(argc, argv);

    test.maxscale->connect();
    test.try_query(test.maxscale->conn_rwsplit, "CREATE SEQUENCE seq");

    std::vector<std::pair<const char*, const char*>> statements =
    {
        {"SELECT NEXT VALUE FOR seq",     "1"},
        {"SELECT PREVIOUS VALUE FOR seq", "1"},
        {"SELECT NEXTVAL(seq)",           "2"},
        {"SELECT LASTVAL(seq)",           "2"},
    };

    for (auto a : statements)
    {
        test.expect(execute_query_check_one(test.maxscale->conn_rwsplit, a.first, a.second) == 0,
                    "Expected '%s' for query: %s",
                    a.second,
                    a.first);
    }

    test.try_query(test.maxscale->conn_rwsplit, "SET SQL_MODE='ORACLE'");

    std::vector<std::pair<const char*, const char*>> oracle_statements =
    {
        {"SELECT seq.nextval", "3"},
        {"SELECT seq.currval", "3"},
    };

    for (auto a : oracle_statements)
    {
        test.expect(execute_query_check_one(test.maxscale->conn_rwsplit, a.first, a.second) == 0,
                    "Expected '%s' for query: %s",
                    a.second,
                    a.first);
    }

    test.try_query(test.maxscale->conn_rwsplit, "DROP SEQUENCE seq");
    test.maxscale->disconnect();

    return test.global_result;
}
