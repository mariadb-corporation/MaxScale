/**
 * MXS-1113: Prepared statement test for schemarouter
 *
 * https://jira.mariadb.org/browse/MXS-1113
 */

#include "testconnections.h"


void test_text_protocol(TestConnections& test, MYSQL* conn)
{

    for (int i = 0; i < test.repl->N; i++)
    {
        test.try_query(conn, "PREPARE stmt%d FROM 'SELECT * FROM shard_db.table%d WHERE fl=3;';", i, i);
        test.try_query(conn, "SET @x = 3;");
        test.try_query(conn, "EXECUTE stmt%d", i);
    }

    for (int i = 0; i < test.repl->N; i++)
    {
        test.try_query(conn, "DEALLOCATE PREPARE stmt%d", i);
    }
}

void test_binary_protocol(TestConnections& test, MYSQL* conn)
{
    const char* query = "SELECT x1, fl FROM shard_db.table2";
    MYSQL_BIND bind[2] = {};
    uint32_t id;
    uint32_t id2;

    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = &id;
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = &id2;

    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    test.add_result(mysql_stmt_prepare(stmt, query, strlen(query)), "Failed to prepare");
    test.add_result(mysql_stmt_execute(stmt), "Failed to execute");
    test.add_result(mysql_stmt_bind_result(stmt, bind), "Failed to bind result");
    test.add_result(mysql_stmt_fetch(stmt), "Failed to fetch result");
    mysql_stmt_close(stmt);
}

int main(int argc, char *argv[])
{
    TestConnections test(argc, argv);
    test.set_timeout(30);
    test.repl->execute_query_all_nodes("STOP SLAVE");
    test.repl->execute_query_all_nodes("DROP DATABASE IF EXISTS shard_db");
    test.repl->execute_query_all_nodes("CREATE DATABASE shard_db");
    MYSQL* conn;

    for (int i = 0; i < test.repl->N; i++)
    {
        conn = open_conn_db(test.repl->port[i], test.repl->IP[i], "shard_db",
                            test.repl->user_name, test.repl->password, test.ssl);
        execute_query(conn, "CREATE TABLE table%d (x1 int, fl int)", i);
        execute_query(conn, "INSERT INTO table%d VALUES(%d, %d)", i, i, i);
        mysql_close(conn);
    }

    test.maxscales->connect_maxscale(0);
    conn = test.maxscales->conn_rwsplit[0];

    test.tprintf("Running text protocol test");
    test_text_protocol(test, conn);
    test.maxscales->disconnect();

    test.maxscales->connect_maxscale(0);
    conn = test.maxscales->conn_rwsplit[0];

    test.tprintf("Running binary protocol test");
    test_binary_protocol(test, conn);

    test.stop_timeout();

    test.maxscales->close_maxscale_connections(0);
    test.repl->execute_query_all_nodes("DROP DATABASE IF EXISTS shard_db");
    test.repl->execute_query_all_nodes("START SLAVE");
    sleep(1);
    test.repl->fix_replication();

    return test.global_result;
}
