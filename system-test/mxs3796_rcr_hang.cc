/**
 * MXS-3796: Hang with readconnroute
 *
 * https://jira.mariadb.org/browse/MXS-3796
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto c = test.maxscale->readconn_master();
    test.expect(c.connect(), "Failed to connect: %s", c.error());

    // We need to execute a query that's large enough to not be returned in one read() call and that is
    // handled by the special SET parser in MaxScale. A query that starts with a comment will always
    // be parsed by it.
    std::string sql = "/* hello */ SELECT '";
    sql.append(100000, 'a');
    sql += "'";

    test.expect(c.query(sql), "Query failed: %s", c.error());

    return test.global_result;
}
