/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * Sanity check for basic functionality
 *
 * Combines several old regression tests into one quick test.
 */

#include <maxtest/testconnections.hh>


void test_rwsplit(TestConnections& test)
{
    test.reset_timeout();
    test.repl->connect();
    std::string master_id = test.repl->get_server_id_str(0);
    test.repl->disconnect();

    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Connection to readwritesplit should succeed");

    // Test simple reads and writes outside of transactions
    test.expect(c.query("CREATE OR REPLACE TABLE table_for_writes(id INT)"),
                "Failed to create table: %s", c.error());

    for (int i = 0; i < 100 && test.ok(); i++)
    {
        if (test.repl->check_backend_versions(100500))
        {
            auto id = c.field("INSERT INTO table_for_writes VALUES (@@server_id) RETURNING id");

            if (test.expect(!id.empty(), "INSERT failed: %s", c.error()))
            {
                test.expect(id == master_id, "INSERT was not routed to master: %s", id.c_str());
            }
        }
        else
        {
            test.expect(c.query("INSERT INTO table_for_writes VALUES (@@server_id)"),
                        "INSERT failed: %s", c.error());
        }
    }


    test.repl->sync_slaves();

    for (int i = 0; i < 100 && test.ok(); i++)
    {
        auto row = c.row("SELECT id, @@server_id FROM table_for_writes");

        if (test.expect(!row.empty(), "SELECT returned no data"))
        {
            test.expect(row[0] == master_id, "Expected %s to be stored in the table, not %s",
                        master_id.c_str(), row[0].c_str());
            test.expect(row[1] != master_id, "SELECT was not routed to a slave");
        }
    }

    test.expect(c.query("DROP TABLE table_for_writes"),
                "Failed to DROP TABLE: %s", c.error());

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
    test.maxscale->connect();
    for (int i = 0; i < 10; i++)
    {
        mysql_stat(test.maxscale->conn_rwsplit);
        test.try_query(test.maxscale->conn_rwsplit, "SELECT 1");
    }

    //
    // MXS-3229: Hang with COM_SET_OPTION
    //

    mysql_set_server_option(test.maxscale->conn_rwsplit, MYSQL_OPTION_MULTI_STATEMENTS_ON);
    mysql_set_server_option(test.maxscale->conn_rwsplit, MYSQL_OPTION_MULTI_STATEMENTS_OFF);

    // Make sure the connection is still OK
    test.try_query(test.maxscale->conn_rwsplit, "SELECT 1");

    test.maxscale->disconnect();
}

void test_mxs3915(TestConnections& test)
{
    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Failed to connect: %s", c.error());
    c.query("SET autocommit=0");
    c.query("COMMIT");
    c.query("SET autocommit=1");

    test.repl->connect();
    auto master_id = test.repl->get_server_id_str(0);

    auto id = c.field("SELECT @@server_id");

    for (int i = 0; i < 10 && id == master_id; i++)
    {
        sleep(1);
        id = c.field("SELECT @@server_id");
    }

    test.expect(id != master_id, "SELECT was routed to master after re-enabling autocommit");
}

void test_mxs4269(TestConnections& test)
{
    auto c = test.maxscale->rwsplit();

    auto check_contents = [&](std::string rows){
        std::string from_slave = c.field("SELECT COUNT(*) FROM test.t1 WHERE server_id = @@server_id");
        test.expect(from_slave == "0", "Slave should not have matching rows but found %s rows",
                    from_slave.c_str());

        from_slave = c.field("SELECT COUNT(*) FROM test.t1");
        test.expect(from_slave == rows, "Slave should have %s rows in total but found %s rows",
                    rows.c_str(), from_slave.c_str());

        c.query("BEGIN");

        std::string from_master = c.field("SELECT COUNT(*) FROM test.t1 WHERE server_id = @@server_id");
        test.expect(from_master == rows, "Master should have %s matching rows but found %s rows",
                    rows.c_str(), from_master.c_str());

        from_master = c.field("SELECT COUNT(*) FROM test.t1");
        test.expect(from_master == rows, "Master should have %s rows but found %s rows",
                    rows.c_str(), from_master.c_str());

        c.query("COMMIT");
    };

    test.expect(c.connect(), "Failed to connect: %s", c.error());
    c.query("CREATE OR REPLACE TABLE test.t1(id INT, server_id INT)");
    test.repl->sync_slaves();

    c.query("SET @var = 1");
    c.query("INSERT INTO test.t1 VALUES (@var := @var + 1, @@server_id)");
    test.repl->sync_slaves();

    check_contents("1");

    c.query("UPDATE test.t1 SET id = (@var := @var + 1), server_id = @@server_id");
    test.repl->sync_slaves();

    check_contents("1");

    c.query("DELETE FROM test.t1 WHERE server_id = @@server_id");
    test.repl->sync_slaves();

    check_contents("0");

    c.query("DROP TABLE test.t1");
}

class Query
{
public:
    static constexpr struct error_t {} ERROR {};

    Query(int line, std::string query, std::string expected)
        : m_line(line)
        , m_query(query)
        , m_expected(expected)
    {
    }

    Query(int line, std::string query, error_t)
        : m_line(line)
        , m_query(query)
        , m_should_fail(true)
    {
    }

    Query(int line, std::string query)
        : m_line(line)
        , m_query(query)
    {
    }

    void send(TestConnections& test, Connection& c)
    {
        test.expect(c.send_query(m_query), "Line %d: Failed to send query '%s': %d, %s",
                    m_line, query(), c.errnum(), c.error());
    }

    void read(TestConnections& test, Connection& c)
    {
        auto res = c.read_query_result_field();

        if (m_should_fail)
        {
            test.expect(!res.has_value(), "Line %d: Query '%s' did not fail", m_line, query());
        }
        else if (test.expect(res.has_value(), "Line %d: Query '%s' failed: %d, %s",
                             m_line, query(), c.errnum(), c.error()))
        {
            test.expect(res.value() == m_expected, "Line %d: Unexpected result for '%s': %s != %s",
                        m_line, query(), res.value().c_str(), m_expected.c_str());
        }
    }

    const char* query() const
    {
        return m_query.c_str();
    }

private:
    int         m_line;
    std::string m_query;
    std::string m_expected;
    bool        m_should_fail {false};
};

#define QUERY(...) Query(__LINE__,  ##__VA_ARGS__)

void test_mxs4419(TestConnections& test)
{
    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Failed to connect: %s", c.error());
    test.expect(c.query("CREATE OR REPLACE TABLE test.t1(id INT)"), "Failed to create table: %s", c.error());

    // The test uses max_slave_connections=1 so we know there's only two possible server IDs that can be
    // returned
    std::string master_id = c.field("SELECT @@server_id, @@last_insert_id");
    std::string slave_id = c.field("SELECT @@server_id");
    test.expect(!master_id.empty(), "Failed to query master ID: %s", c.error());
    test.expect(!slave_id.empty(), "Failed to query slave ID: %s", c.error());

    std::vector<Query> rw_trx_queries
    {
        // Initial transaction, should be pipelined to the master
        QUERY("START TRANSACTION"),
        QUERY("INSERT INTO t1 VALUES (1)"),
        QUERY("COMMIT"),

        // Should get routed to the master
        QUERY("SELECT @@server_id, MAX(id), @@last_insert_id FROM test.t1", master_id),

        // The following should get routed to a slave server
        QUERY("SELECT @@server_id", slave_id),
        QUERY("SELECT @@server_id", slave_id),
        QUERY("SELECT @@server_id", slave_id),
        QUERY("SELECT @@server_id", slave_id),
        QUERY("SELECT @@server_id", slave_id),
        QUERY("SELECT @@server_id", slave_id),
        QUERY("SELECT @@server_id", slave_id),
        QUERY("SELECT @@server_id", slave_id),
        QUERY("SELECT 2", "2"),

        // The following should get routed the master
        QUERY("BEGIN"),
        QUERY("SELECT @@server_id", master_id),
        QUERY("COMMIT"),

        // The transaction should again be routed to the master
        QUERY("BEGIN"),
        QUERY("INSERT INTO t1 VALUES (2)"),
        QUERY("ROLLBACK"),

        // The rollback should make sure the row isn't added
        QUERY("SELECT MAX(id), @@last_insert_id FROM test.t1", "1"),

        // Reads should get routed to a slave
        QUERY("SELECT 1", "1"),

        // Disabling autocommit should route queries to the master until it is disabled again
        QUERY("SET autocommit=0"),
        QUERY("SELECT @@server_id", master_id),
        QUERY("SET autocommit=1"),

        // With autocommit enabled, the read should go to a slave
        QUERY("SELECT @@server_id", slave_id),
    };

    for (auto q : rw_trx_queries)
    {
        q.send(test, c);
    }

    for (auto q : rw_trx_queries)
    {
        q.read(test, c);
    }

    std::vector<Query> ro_trx_queries
    {
        // Read-only transaction, should be routed in its entirety to the same slave
        QUERY("START TRANSACTION READ ONLY"),
        QUERY("SELECT @@server_id", slave_id),
        QUERY("SELECT @@server_id", slave_id),
        QUERY("SELECT @@server_id", slave_id),
        QUERY("SELECT @@server_id", slave_id),
        QUERY("SELECT @@server_id", slave_id),
        QUERY("SELECT 1", "1"),
        QUERY("COMMIT"),

        // This should get routed to a master
        QUERY("INSERT INTO t1 VALUES (IF(@@server_id = " + master_id
              + ", SLEEP(1) + 2, (SELECT engine FROM information_schema.engines)))"),

        // Should get routed to the slave once the INSERT completes
        QUERY("START TRANSACTION READ ONLY"),
        QUERY("SELECT 2", "2"),
        QUERY("SELECT @@server_id", slave_id),
        QUERY("ROLLBACK"),

        // This should only make one transaction read-only
        QUERY("SET TRANSACTION READ ONLY"),
        QUERY("BEGIN"),
        QUERY("INSERT INTO t1 VALUES (123)", Query::ERROR),
        QUERY("COMMIT"),
        QUERY("BEGIN"),
        QUERY("SELECT @@server_id", master_id),
        QUERY("COMMIT"),

        // This makes all transactions read-only
        QUERY("SET SESSION TRANSACTION READ ONLY"),
        QUERY("BEGIN"),
        QUERY("SELECT @@server_id", slave_id),
        QUERY("COMMIT"),
        QUERY("BEGIN"),
        QUERY("SELECT @@server_id", slave_id),
        QUERY("COMMIT"),
        QUERY("SET SESSION TRANSACTION READ WRITE"),
    };

    for (auto q : ro_trx_queries)
    {
        q.send(test, c);
    }

    for (auto q : ro_trx_queries)
    {
        q.read(test, c);
    }

    c.query("DROP TABLE test.t1");
}

void mxs4843_lots_of_connection_attributes(TestConnections& test)
{
    auto c = test.maxscale->rwsplit();
    size_t len = 0;
    int i = 0;

    while (len < 1024 * 1024 * 14)
    {
        auto str = std::to_string(i++);
        auto key = "key" + str;
        auto value = "value" + str;
        c.set_connection_attr(key, value);
        len += key.length() + value.length() + 2;
    }

    test.expect(c.connect(), "Failed to connect: %s", c.error());
    test.expect(c.query("SELECT 1"), "Failed to query: %s", c.error());
}

#define CHECK(expr) if (!(expr)) throw std::runtime_error(#expr);

void test_mxs4981(TestConnections& test)
{
    try
    {
        auto c = test.maxscale->rwsplit();
        CHECK(c.connect());
        auto id = c.field("SELECT @@server_id, @@last_insert_id");

        for (int i = 0; i < 200; i++)
        {
            CHECK(c.send_query("SET @a = (SELECT SLEEP(CASE @@server_id WHEN " + id
                               + " THEN 0 ELSE 2 END))"));
        }

        for (int i = 0; i < 200; i++)
        {
            CHECK(c.read_query_result());
        }

        for (int i = 0; i < 20; i++)
        {
            CHECK(c.change_user(test.maxscale->user_name(), test.maxscale->password()));
        }
    }
    catch (const std::runtime_error& e)
    {
        test.add_failure("%s", e.what());
    }
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto connections = [&]() {
            return test.maxctrl("api get servers/server1 data.attributes.statistics.connections").output;
        };

    test.expect(connections()[0] == '0', "The master should have no connections");
    test.maxscale->connect();
    test.expect(connections()[0] == '2', "The master should have two connections");
    test.maxscale->disconnect();
    test.expect(connections()[0] == '0', "The master should have no connections");

    test.maxscale->connect();
    for (auto a : {"show status", "show variables", "show global status"})
    {
        for (int i = 0; i < 10; i++)
        {
            test.try_query(test.maxscale->conn_rwsplit, "%s", a);
            test.try_query(test.maxscale->conn_master, "%s", a);
        }
    }
    test.maxscale->disconnect();

    // Readwritesplit sanity checks
    test_rwsplit(test);

    // MXS-3915: Autocommit tracking is broken
    test_mxs3915(test);

    // MXS-4269: UPDATEs with user variable modifications are treated as session commands
    test_mxs4269(test);

    // MXS-4419: Pipelined transactions are not tracked correctly
    test_mxs4419(test);

    // MXS-4843: Check that large sets of connection attributes are accepted
    mxs4843_lots_of_connection_attributes(test);

    // MXS-4981: Large amounts of session commands will prevent MaxScale from stopping.
    test_mxs4981(test);

    return test.global_result;
}
