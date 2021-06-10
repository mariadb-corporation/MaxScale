/**
 * MXS-3416: Extra OK packet when session command is followed by a causal read
 *
 * https://jira.mariadb.org/browse/MXS-3416
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections::require_repl_version("10.3.8");
    TestConnections test(argc, argv);

    auto conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Connection failed: %s", conn.error());

    for (int i = 0; i < 1000 && test.ok(); i++)
    {
        test.reset_timeout();
        test.expect(conn.query("SET @a = 1"), "SET should work: %s", conn.error());
        auto res = conn.field("SELECT 2 as two");
        test.expect(res == "2", "Iteration %d: SELECT retured: %s",
                    i, res.empty() ? "an empty string" : res.c_str());
    }

    return test.global_result;
}
