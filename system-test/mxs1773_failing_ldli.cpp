/**
 * MXS-1773: Failing LOAD DATA LOCAL INFILE confuses readwritesplit
 *
 * https://jira.mariadb.org/browse/MXS-1773
 */
#include <maxtest/testconnections.hh>
#include <functional>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.maxscale->connect();
    auto q = std::bind(execute_query, test.maxscale->conn_rwsplit, std::placeholders::_1);
    q("LOAD DATA LOCAL INFILE '/tmp/this-file-does-not-exist.txt' INTO TABLE this_table_does_not_exist");
    q("SELECT 1");
    q("SELECT 2");
    q("SELECT 3");
    test.maxscale->disconnect();

    return test.global_result;
}
