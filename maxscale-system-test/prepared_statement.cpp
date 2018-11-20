/**
 * @file prepared_statement.cpp Checks if prepared statement works via Maxscale
 *
 * - Create table t1 and fill it ith some data
 * - via RWSplit:
 *   + PREPARE stmt FROM 'SELECT * FROM t1 WHERE fl=@x;';
 *   + SET @x = 3;")
 *   + EXECUTE stmt")
 *   + SET @x = 4;")
 *   + EXECUTE stmt")
 * - check if Maxscale is alive
 */

#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

void test_basic(TestConnections& test)
{
    test.set_timeout(60);
    int N = 4;

    test.repl->connect();
    test.maxscales->connect_maxscale(0);

    create_t1(test.maxscales->conn_rwsplit[0]);
    insert_into_t1(test.maxscales->conn_rwsplit[0], N);

    test.set_timeout(20);
    test.try_query(test.maxscales->conn_rwsplit[0], "PREPARE stmt FROM 'SELECT * FROM t1 WHERE fl=@x;';");
    test.try_query(test.maxscales->conn_rwsplit[0], "SET @x = 3;");
    test.try_query(test.maxscales->conn_rwsplit[0], "EXECUTE stmt");
    test.try_query(test.maxscales->conn_rwsplit[0], "SET @x = 4;");
    test.try_query(test.maxscales->conn_rwsplit[0], "EXECUTE stmt");

    test.check_maxscale_alive(0);
    test.stop_timeout();
}

void test_routing(TestConnections& test)
{
    test.set_timeout(60);
    test.repl->connect();
    int server_id = test.repl->get_server_id(0);
    test.maxscales->connect_maxscale(0);

    // Test that reads are routed to slaves
    char buf[1024] = "-1";
    test.try_query(test.maxscales->conn_rwsplit[0], "PREPARE ps1 FROM 'SELECT @@server_id'");
    test.add_result(find_field(test.maxscales->conn_rwsplit[0], "EXECUTE ps1", "@@server_id", buf),
                    "Execute should succeed");
    int res = atoi(buf);
    test.add_result(res == server_id,
                    "Query should be routed to a slave (got %d, master is %d)",
                    res,
                    server_id);


    // Test reads inside transactions are routed to master
    strcpy(buf, "-1");
    test.try_query(test.maxscales->conn_rwsplit[0], "BEGIN");
    test.add_result(find_field(test.maxscales->conn_rwsplit[0], "EXECUTE ps1", "@@server_id", buf),
                    "Execute should succeed");
    res = atoi(buf);
    test.add_result(res != server_id,
                    "Query should be routed to master inside a transaction (got %d, master is %d)",
                    res,
                    server_id);
    test.try_query(test.maxscales->conn_rwsplit[0], "COMMIT");

    // Test reads inside read-only transactions are routed slaves
    strcpy(buf, "-1");
    test.try_query(test.maxscales->conn_rwsplit[0], "START TRANSACTION READ ONLY");
    test.add_result(find_field(test.maxscales->conn_rwsplit[0], "EXECUTE ps1", "@@server_id", buf),
                    "Execute should succeed");
    res = atoi(buf);
    test.add_result(res == server_id,
                    "Query should be routed to a slave inside a read-only transaction (got %d, master is %d)",
                    res,
                    server_id);
    test.try_query(test.maxscales->conn_rwsplit[0], "COMMIT");

    // Test prepared statements that modify data
    strcpy(buf, "-1");
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE OR REPLACE TABLE test.t1 (id INT)");
    test.try_query(test.maxscales->conn_rwsplit[0], "PREPARE ps2 FROM 'INSERT INTO test.t1 VALUES (?)'");
    test.try_query(test.maxscales->conn_rwsplit[0], "SET @a = @@server_id");
    test.try_query(test.maxscales->conn_rwsplit[0], "EXECUTE ps2 USING @a");
    test.repl->sync_slaves();
    test.add_result(find_field(test.maxscales->conn_rwsplit[0], "SELECT id FROM test.t1", "id", buf),
                    "Read should succeed");
    res = atoi(buf);
    test.add_result(res != server_id,
                    "Writes should be routed to the master (got %d, master is %d)",
                    res,
                    server_id);

    // Cleanup
    test.check_maxscale_alive(0);
    test.stop_timeout();
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    test.tprintf("Running basic test");
    test_basic(test);

    test.tprintf("Running text PS routing test");
    test_routing(test);

    return test.global_result;
}
