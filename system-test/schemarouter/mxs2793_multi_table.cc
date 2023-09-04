#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.repl->connect();

    for (int i = 0; i < 4; i++)
    {
        execute_query_silent(test.repl->nodes[i], "STOP SLAVE");
    }

    // Create individual databases for all nodes as well as a common shared database.
    for (int i = 0; i < 4; i++)
    {
        test.try_query(test.repl->nodes[i], "CREATE DATABASE db%d", i);
        test.try_query(test.repl->nodes[i], "CREATE TABLE db%d.t1(id INT)", i);
        test.try_query(test.repl->nodes[i], "INSERT INTO db%d.t1 VALUES (@@server_id)", i);

        test.try_query(test.repl->nodes[i], "CREATE DATABASE common");
        test.try_query(test.repl->nodes[i], "CREATE TABLE common.t1(id INT)");
        test.try_query(test.repl->nodes[i], "INSERT INTO common.t1 VALUES (@@server_id)");
    }

    // Create a database that is on two nodes
    for (int i = 1; i < 3; i++)
    {
        test.try_query(test.repl->nodes[i], "CREATE DATABASE partially_shared");
        test.try_query(test.repl->nodes[i], "CREATE TABLE partially_shared.t1(id INT)");
        test.try_query(test.repl->nodes[i], "INSERT INTO partially_shared.t1 VALUES(@@server_id)");
    }

    auto conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Connection should work: %s", conn.error());

    // Queries that target a shared and unique dataase should be routed to the node that has it.
    test.expect(conn.query("SELECT a.id, b.id FROM db0.t1 AS a JOIN common.t1 AS b ON (1 = 1)"),
                "Query to database db0 failed: %s", conn.error());
    test.expect(conn.query("SELECT a.id, b.id FROM db1.t1 AS a JOIN common.t1 AS b ON (1 = 1)"),
                "Query to database db2 failed: %s", conn.error());
    test.expect(conn.query("SELECT a.id, b.id FROM db2.t1 AS a JOIN common.t1 AS b ON (1 = 1)"),
                "Query to database db3 failed: %s", conn.error());
    test.expect(conn.query("SELECT a.id, b.id FROM db3.t1 AS a JOIN common.t1 AS b ON (1 = 1)"),
                "Query to database db4 failed: %s", conn.error());

    // A query targeting the partially shared table should be routed to any of the two nodes that contain it
    test.expect(conn.query("SELECT b.id, c.id FROM "
                           "common.t1 AS b JOIN partially_shared.t1 AS c ON (1 = 1)"),
                "Query to database db4 failed: %s", conn.error());

    // A query with a fully shared, a partially shared and a unique database should be routed to the node with
    // the unique database
    test.expect(conn.query("SELECT a.id, b.id, c.id FROM db2.t1 AS a JOIN common.t1 AS b "
                           "JOIN partially_shared.t1 AS c ON (1 = 1)"),
                "Query to database db4 failed: %s", conn.error());

    for (int i = 1; i < 3; i++)
    {
        test.try_query(test.repl->nodes[i], "DROP DATABASE partially_shared");
    }

    for (int i = 0; i < 4; i++)
    {
        test.try_query(test.repl->nodes[i], "DROP DATABASE db%d", i);
        test.try_query(test.repl->nodes[i], "DROP DATABASE common");
    }

    return test.global_result;
}
