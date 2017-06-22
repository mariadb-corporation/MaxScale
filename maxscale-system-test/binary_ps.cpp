/**
 * Test binary protocol prepared statement routing
 */
#include "testconnections.h"

int main(int argc, char** argv)
{

    TestConnections test(argc, argv);
    char server_id[test.galera->N][1024];

    test.repl->connect();

    /** Get server_id for each node */
    for (int i = 0; i < test.repl->N; i++)
    {
        sprintf(server_id[i], "%d", test.repl->get_server_id(i));
    }

    test.connect_maxscale();

    test.set_timeout(20);

    MYSQL_STMT* stmt = mysql_stmt_init(test.conn_rwsplit);
    const char* write_query = "SELECT @@server_id, @@last_insert_id";
    const char* read_query = "SELECT @@server_id";
    char buffer[100] = "";
    char buffer2[100] = "";
    my_bool err = false;
    my_bool isnull = false;
    MYSQL_BIND bind[2] = {};

    bind[0].buffer_length = sizeof(buffer);
    bind[0].buffer = buffer;
    bind[0].error = &err;
    bind[0].is_null = &isnull;
    bind[1].buffer_length = sizeof(buffer2);
    bind[1].buffer = buffer2;
    bind[1].error = &err;
    bind[1].is_null = &isnull;

    // Execute a write, should return the master's server ID
    test.add_result(mysql_stmt_prepare(stmt, write_query, strlen(write_query)), "Failed to prepare");
    test.add_result(mysql_stmt_execute(stmt), "Failed to execute");
    test.add_result(mysql_stmt_bind_result(stmt, bind), "Failed to bind result");
    test.add_result(mysql_stmt_fetch(stmt), "Failed to fetch result");
    test.add_result(strcmp(buffer, server_id[0]), "Expected server_id '%s', got '%s'", server_id[0], buffer);

    mysql_stmt_close(stmt);
    stmt = mysql_stmt_init(test.conn_rwsplit);

    // Execute read, should return a slave server ID
    test.add_result(mysql_stmt_prepare(stmt, read_query, strlen(read_query)), "Failed to prepare");
    test.add_result(mysql_stmt_execute(stmt), "Failed to execute");
    test.add_result(mysql_stmt_bind_result(stmt, bind), "Failed to bind result");
    test.add_result(mysql_stmt_fetch(stmt), "Failed to fetch result");
    test.add_result(strcmp(buffer, server_id[1]) && strcmp(buffer, server_id[2]) && strcmp(buffer, server_id[3]),
                    "Expected one of the slave server IDs (%s, %s or %s), not '%s'",
                    server_id[1], server_id[2], server_id[3], buffer);


    mysql_stmt_close(stmt);

    test.close_maxscale_connections();

    return test.global_result;
}
