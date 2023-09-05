/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * Test binary protocol prepared statement routing
 */
#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    char server_id[test.repl->N][1024];

    test.repl->connect();

    /** Get server_id for each node */
    for (int i = 0; i < test.repl->N; i++)
    {
        sprintf(server_id[i], "%d", test.repl->get_server_id(i));
    }

    test.maxscale->connect_maxscale();

    test.reset_timeout();

    MYSQL_STMT* stmt = mysql_stmt_init(test.maxscale->conn_rwsplit);
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

    stmt = mysql_stmt_init(test.maxscale->conn_rwsplit);

    // Execute read, should return a slave server ID
    test.add_result(mysql_stmt_prepare(stmt, read_query, strlen(read_query)), "Failed to prepare");
    // Sleep for a while to make sure all servers have processed the COM_STMT_PREPARE
    sleep(1);
    test.add_result(mysql_stmt_execute(stmt), "Failed to execute");
    test.add_result(mysql_stmt_bind_result(stmt, bind), "Failed to bind result");
    test.add_result(mysql_stmt_fetch(stmt), "Failed to fetch result");
    std::string server_ids;
    bool found = false;
    for (int i = 1; i < test.repl->N; ++i)
    {
        if (strcmp(buffer, server_id[i]) == 0)
        {
            found = true;
        }

        if (!server_ids.empty())
        {
            server_ids += ", ";
        }

        server_ids += server_id[i];
    }

    test.expect(found, "Expected one of the slave server IDs (%s), not '%s'",
                server_ids.c_str(), buffer);

    mysql_stmt_close(stmt);

    test.maxscale->close_maxscale_connections();

    // MXS-2266: COM_STMT_CLOSE causes a warning to be logged
    test.log_excludes("Closing unknown prepared statement");

    return test.global_result;
}
