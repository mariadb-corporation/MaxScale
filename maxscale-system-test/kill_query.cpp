/**
 * Test KILL QUERY functionality
 */

#include "testconnections.h"

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    auto conn = test.maxscales->rwsplit();
    conn.connect();
    conn.query("CREATE OR REPLACE TABLE test.t1 (id LONGTEXT)");

    for (int x = 0; x < 10; x++)
    {
        conn.query("INSERT INTO test.t1 VALUES (REPEAT('a', 5000000))");
    }

    for (int i = 0; i < 3; i++)
    {
        auto a = test.maxscales->rwsplit();
        auto b = test.maxscales->rwsplit();
        test.expect(a.connect() && b.connect(), "Connections should work");

        auto id = a.thread_id();

        test.set_timeout(15);
        std::thread thr(
            [&]() {
                // The ALTER should take longer than 15 seconds to complete so that a KILL is required to
                // interrupt it.
                test.expect(!a.query("ALTER TABLE test.t1 FORCE"), "ALTER should fail");

                const char* expected = "Query execution was interrupted";
                test.expect(strstr(a.error(), expected),
                            "Alter should fail with '%s' but it failed with '%s'",
                            expected, a.error());
            });

        test.expect(b.query("KILL QUERY " + std::to_string(id)), "KILL QUERY failed: %s", b.error());
        thr.join();

        test.stop_timeout();
    }

    conn.query("DROP TABLE test.t1");

    return test.global_result;
}
