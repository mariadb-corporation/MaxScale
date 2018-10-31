/**
 * Sanity check for basic functionality
 *
 * Combines several old regression tests into one quick test.
 */

#include "testconnections.h"


void test_rwsplit(TestConnections& test)
{
    test.set_timeout(300);
    test.repl->connect();
    std::string master_id = test.repl->get_server_id_str(0);
    test.repl->disconnect();

    auto c = test.maxscales->rwsplit();
    test.expect(c.connect(), "Connection to readwritesplit should succeed");

    // Transactions to master
    c.query("START TRANSACTION");
    test.expect(c.field("SELECT @@server_id") == master_id,
                "START TRANSACTION should go to the master");
    c.query("COMMIT");

    // Read-only transactions to slave
    c.query("START TRANSACTION READ ONLY");
    test.expect(c.field("SELECT @@server_id") != master_id,
                "START TRANSACTION READ ONLY should go to a slave");
    c.query("COMMIT");

    // @@last_insert_id routed to master
    test.expect(c.field("SELECT @@server_id, @@last_insert_id") == master_id,
                "@@last_insert_id should go to the master");
    test.expect(c.field("SELECT last_insert_id(), @@server_id", 1) == master_id,
                "@@last_insert_id should go to the master");

    // Replication related queries
    test.expect(!c.row("SHOW SLAVE STATUS").empty(),
                "SHOW SLAVE STATUS should go to a slave");

    // User variable modification in SELECT
    test.expect(!c.query("SELECT @a:=@a+1 as a, user FROM mysql"),
                "Query with variable modification should fail");

    // Repeated session commands
    for (int i = 0; i < 10000; i++)
    {
        test.expect(c.query("set @test=" + std::to_string(i)), "SET should work: %s", c.error());
    }

    // Large result sets
    for (int i = 1; i < 5000; i += 7)
    {
        c.query("SELECT REPEAT('a'," + std::to_string(i) + ")");
    }

    // Non ASCII characters
    c.query("CREATE OR REPLACE TABLE test.t1 AS SELECT 'Кот'");
    c.query("BEGIN");
    c.check("SELECT * FROM test.t1", "Кот");
    c.query("COMMIT");
    c.query("DROP TABLE test.t1");

    // Temporary tables
    for (auto a : {
        "USE test",
        "CREATE OR REPLACE TABLE t1(`id` INT(10) UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY)",
        "CREATE OR REPLACE TABLE t2(`id` INT(10) UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY)",
        "CREATE TEMPORARY TABLE temp1(`id` INT(10) UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY)",
        "INSERT INTO temp1 values (1), (2), (3)",
        "INSERT INTO t1 values (1), (2), (3)",
        "INSERT INTO t2 values (1), (2), (3)",
        "CREATE TEMPORARY TABLE temp2 SELECT DISTINCT p.id FROM temp1 p JOIN t1 t "
        "    ON (t.id = p.id) LEFT JOIN t2 ON (t.id = t2.id) WHERE p.id IS NOT NULL "
        "    AND @@server_id IS NOT NULL",
        "SELECT * FROM temp2",
        "DROP TABLE t1",
        "DROP TABLE t2"
    })
    {
        test.expect(c.query(a), "Temp table query failed");
    }

    //  Temporary and real table overlap
    c.query("CREATE OR REPLACE TABLE test.t1 AS SELECT 1 AS id");
    c.query("CREATE TEMPORARY TABLE test.t1 AS SELECT 2 AS id");
    c.check("SELECT id FROM test.t1", "2");
    c.query("DROP TABLE test.t1");
    c.query("DROP TABLE test.t1");

    // COM_STATISTICS
    test.maxscales->connect();
    for (int i = 0; i < 10; i++)
    {
        mysql_stat(test.maxscales->conn_rwsplit[0]);
        test.try_query(test.maxscales->conn_rwsplit[0], "SELECT 1");
    }
    test.maxscales->disconnect();
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto connections = [&]() {
            return test.maxctrl("api get servers/server1 data.attributes.statistics.connections").second;
        };

    test.expect(connections()[0] == '0', "The master should have no connections");
    test.maxscales->connect();
    test.expect(connections()[0] == '2', "The master should have two connections");
    test.maxscales->disconnect();
    test.expect(connections()[0] == '0', "The master should have no connections");

    test.maxscales->connect();
    for (auto a : {"show status", "show variables", "show global status"})
    {
        for (int i = 0; i < 10; i++)
        {
            test.try_query(test.maxscales->conn_rwsplit[0], "%s", a);
            test.try_query(test.maxscales->conn_master[0], "%s", a);
        }
    }
    test.maxscales->disconnect();

    // Readwritesplit sanity checks
    test_rwsplit(test);

    return test.global_result;
}
