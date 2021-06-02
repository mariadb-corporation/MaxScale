/**
 * Test smartrouter routing to readwritesplit services
 */
#include <maxtest/testconnections.hh>
#include <sstream>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.repl->connect();
    auto ids = test.repl->get_all_server_ids_str();
    test.repl->disconnect();

    auto conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Connection should work: %s", conn.error());

    test.log_printf("Test 1: Basic routing");
    test.reset_timeout();

    std::vector<std::string> queries =
    {
        "SELECT 1",
        "SELECT @@server_id",
        "SELECT @@last_insert_id",
        "SELECT SLEEP(1)",
        "BEGIN",
        "USE test",
        "COMMIT",
        "CREATE OR REPLACE TABLE test.t1(id INT)",
        "BEGIN",
        "INSERT INTO test.t1 VALUES (1), (2), (3)",
        "SELECT * FROM test.t1",
        "COMMIT",
        "SELECT * FROM test.t1",
        "DROP TABLE test.t1",
    };

    for (auto q : queries)
    {
        test.expect(conn.query(q), "Query failed: %s", conn.error());
    }

    test.log_printf("Test 2: Query measurement");
    test.reset_timeout();

    test.expect(conn.connect(), "Reconnection should work: %s", conn.error());
    test.expect(conn.query("CREATE OR REPLACE TABLE test.t2(id INT) ENGINE=MyISAM"), "CREATE failed: %s", conn.error());

    std::ostringstream ss;
    ss << "INSERT INTO test.t2 VALUES (0) ";

    for (int i = 1; i < 5000; i++)
    {
        ss << ", (" << i << ")";
    }

    test.expect(conn.query(ss.str()), "INSERT failed: %s", conn.error());

    test.repl->sync_slaves();

    auto srv = test.repl->get_connection(2);
    srv.connect();
    srv.query("TRUNCATE test.t2");
    srv.query("INSERT INTO test.t2 VALUES (2)");

    test.expect(conn.connect(), "Reconnection should work: %s", conn.error());

    // This is pretty much guaranteed to never complete on any of the servers except the one where the truncated the table
    auto response = conn.field("SELECT @@server_id, a.id + b.id FROM test.t2 AS a JOIN test.t2 AS b WHERE a.id <= b.id", 0);

    test.expect(response == ids[2],
                "@@server_id mismatch: %s (response) != %s (server3) [%s]",
                response.c_str(), ids[2].c_str(), conn.error());

    conn.query("DROP TABLE test.t2");

    return test.global_result;
}
