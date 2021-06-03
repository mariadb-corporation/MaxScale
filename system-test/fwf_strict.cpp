/**
 * MXS-1111: Dbfwfilter COM_PING test
 *
 * Check that COM_PING is allowed with `action=allow`
 */

#include <maxtest/testconnections.hh>

#include <fstream>

auto rules =
    R"(
rule dont_mess_with_system_tables match regex 'mysql.*' on_queries drop|alter|create|use|load
users %@% match any rules dont_mess_with_system_tables
)";

int main(int argc, char** argv)
{
    std::ofstream file("rules.txt");
    file << rules;
    file.close();

    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);
    test.maxscales->copy_fw_rules("rules.txt", ".");
    test.maxscales->start();

    auto conn = test.maxscales->rwsplit();
    test.expect(conn.connect(), "Connect failed: %s", conn.error());
    test.expect(conn.query("SELECT 1; SELECT 2; SELECT 3;"), "Query failed: %s", conn.error());
    test.expect(!conn.query("DROP DATABASE mysql"), "DROP succeeded");

    return test.global_result;
}
