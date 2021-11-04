/**
 * @file sharding.cpp - Schema router test and regression test for MXS-78, MXS-79
 *
 * @verbatim
 *  [MySQL Monitor]
 *  type=monitor
 *  module=mysqlmon
 *  servers= server1, server2,server3  ,server4
 *  user=skysql
 *  passwd= skysql
 *
 *  [Sharding router]
 *  type=service
 *  router=schemarouter
 *  servers=server1,     server2,              server3,server4
 *  user=skysql
 *  passwd=skysql
 *  auth_all_servers=1
 *  filters=QLA
 *
 *  @endverbatim
 * - stop all slaves in Master/Slave setup
 * - restrt Maxscale
 * - using direct connection to backend nodes
 *    - create user0...userN users on all nodes
 *    - create sharddb on all nodes
 *    - create database 'shard_db%d" on node %d (% from 0 to N)
 *    - GRANT SELECT,USAGE,CREATE ON shard_db.* TO 'user%d'@'%%' only on node %d
 * - for every user%d
 *   - open connection to schemarouter using user%d
 * - CREATE TABLE table%d (x1 int, fl int)
 * - check if Maxscale alive
 */


#include <iostream>
#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.reset_timeout();
    const char opening_fmt[] = "Opening connection to sharding router using user '%s', "
                               "password '%s' and db '%s'.\n";
    const char common_db[] = "common_db"; // This db will be on all backends.
    const char shard_db_fmt[] = "shard_db_%i"; // Server-specific db name prefix
    const char shard_table_fmt[] = "shard_table_%i";
    const char user_name_fmt[] = "test_user_%i";
    const char user_pw_fmt[] = "test_passwd_%i";

    const int N = test.repl->N; // Number of nodes and users.
    const int LEN = 20;

    // Generate various strings.
    char shard_dbs[N][LEN];
    char shard_tables[N][LEN];
    char user_names[N][LEN];
    char user_pws[N][LEN];

    for (int i = 0; i < N; i++)
    {
        sprintf(shard_dbs[i], shard_db_fmt, i);
        sprintf(shard_tables[i], shard_table_fmt, i);
        sprintf(user_names[i], user_name_fmt, i);
        sprintf(user_pws[i], user_pw_fmt, i);
    }

    test.repl->execute_query_all_nodes("STOP SLAVE");
    test.repl->connect();
    test.reset_timeout();

    // On every node...
    for (int i = 0; i < N; i++)
    {
        test.tprintf("\nNode %i:\n----------\n", i);
        auto node = test.repl->nodes[i];
        // for every user...
        for (int j = 0; j < N; j++)     // users
        {
            // create users
            auto user = user_names[j];
            auto pass = user_pws[j];
            if (test.try_query(node, "CREATE OR REPLACE USER '%s'@'%%' IDENTIFIED BY '%s';", user, pass) == 0)
            {
                test.tprintf("Created user '%s'.", user);
            }
        }

        auto shard_db = shard_dbs[i];
        if (test.try_query(node, "CREATE OR REPLACE DATABASE %s", common_db) == 0
            && test.try_query(node, "CREATE OR REPLACE DATABASE %s", shard_db) == 0)
        {
            test.tprintf("Created databases '%s' and '%s'.", common_db, shard_db);
        }

        // Grant one user access to the common db on this node. Only the main test user has access to
        // server-specific db:s.
        test.try_query(node, "GRANT SELECT,USAGE,CREATE ON %s.* TO '%s'@'%%'", common_db, user_names[i]);
        test.try_query(node, "FLUSH PRIVILEGES");
    }
    test.tprintf("%s", "----------\n");

    test.repl->close_connections();
    sleep(6); // The router is configured to refresh the shard map if older than 5 seconds.
    auto mxs_ip = test.maxscale->ip4();

    // Generate a table for each user on the common db. The tables should be on different backends since
    // each user only has access to one node.
    // Here, 'rwsplit_port' etc are actually ports to the schemarouter.
    for (int i = 0; i < test.repl->N; i++)
    {
        auto user = user_names[i];
        auto pass = user_pws[i];
        auto table = shard_tables[i];

        test.tprintf(opening_fmt, user, pass, common_db);
        auto conn = open_conn_db(test.maxscale->rwsplit_port, mxs_ip,
                                 common_db, user, pass, test.maxscale_ssl);
        if (test.try_query(conn, "CREATE TABLE %s (x1 int, fl int);", table) == 0)
        {
            test.tprintf("Table '%s.%s' for user '%s' created.", common_db, table, user);
        }
        mysql_close(conn);
    }

    sleep(6); // Again, wait for shard info update.

    for (int i = 0; i < N; i++)
    {
        auto user = user_names[i];
        auto pass = user_pws[i];
        auto table = shard_tables[i];
        test.tprintf(opening_fmt, user, pass, common_db);
        auto conn = open_conn_db(test.maxscale->rwsplit_port, mxs_ip,
                                 common_db, user, pass, test.maxscale_ssl);

        const char* query = "SHOW TABLES;";
        test.tprintf("Table should be %s\n", table);
        test.add_result(execute_query_check_one(conn, query, table), "check failed\n");
        mysql_close(conn);
    }

    // Test accessing all databases as the admin.
    test.maxscale->connect_rwsplit();
    auto conn = test.maxscale->conn_rwsplit; // Is a schemarouter connection.
    test.try_query(conn, "USE %s", common_db);
    for (int i = 0; i < N; i++)
    {
        test.try_query(conn, "USE %s", shard_dbs[i]);
    }
    if (test.ok())
    {
        test.tprintf("%s", "All databases are present.");
    }
    test.maxscale->close_rwsplit();

    test.tprintf("Test connecting with empty database name for all users.\n");
    for (int i = 0; i < N; i++)
    {
        auto user = user_names[i];
        auto pass = user_pws[i];
        conn = open_conn_db(test.maxscale->rwsplit_port, mxs_ip,
                            "", user, pass, test.maxscale_ssl);
        test.expect(conn, "Connection failed for user '%s'.", user);
        mysql_close(conn);
    }
    if (test.ok())
    {
        test.tprintf("Connections succeeded.");
    }

    test.log_excludes("Length (0) is 0");
    test.log_excludes("Unable to parse query");
    test.log_excludes("query string allocation failed");

    test.repl->connect();
    /** Cleanup */
    for (int i = 0; i < N; i++)
    {
        conn = test.repl->nodes[i];
        for (int j = 0; j < N; j++)
        {
            test.try_query(conn, "DROP USER '%s'@'%%';", user_names[j]);
        }

        test.try_query(conn, "DROP DATABASE %s", common_db);
        test.try_query(conn, "DROP DATABASE %s", shard_dbs[i]);
    }

    test.repl->execute_query_all_nodes("START SLAVE");
    return test.global_result;
}
