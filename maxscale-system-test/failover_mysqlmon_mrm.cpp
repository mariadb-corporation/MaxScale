/**
 * Test replication-manager
 */

#include "testconnections.h"

void get_output(TestConnections& test)
{
    int ec;
    test.tprintf("Maxadmin output:");
    char *output = test.maxscales->ssh_node_output(0, "maxadmin list servers", true, &ec);
    test.tprintf("%s", output);
    free(output);

    test.tprintf("MaxScale output:");
    output = test.maxscales->ssh_node_output(0, "cat /var/log/maxscale/maxscale.log && "
                                            "sudo truncate -s 0 /var/log/maxscale/maxscale.log",
                                             true, &ec);
    test.tprintf("%s", output);
    free(output);
}

static int inserts = 0;

void check(TestConnections& test)
{
    MYSQL *conn = test.maxscales->open_rwsplit_connection(0);
    const char *query1 = "INSERT INTO test.t1 VALUES (%d)";
    const char *query2 = "SELECT * FROM test.t1";

    test.try_query(conn, "BEGIN");
    test.tprintf(query1, inserts);
    test.try_query(conn, query1, inserts++);
    mysql_query(conn, query2);

    MYSQL_RES *res = mysql_store_result(conn);
    test.add_result(res == NULL, "Query should return a result set");

    if (res)
    {
        std::string values;
        MYSQL_ROW row;
        int num_rows = mysql_num_rows(res);
        test.add_result(num_rows != inserts, "Query returned %d rows when %d rows were expected",
                        num_rows, inserts);
        const char *separator = "";

        while ((row = mysql_fetch_row(res)))
        {
            values += separator;
            values += row[0];
            separator = ", ";
        }
        test.tprintf("%s: %s", query2, values.c_str());
    }
    test.try_query(conn, "COMMIT");
    mysql_close(conn);
}

/**
 * Get master server id (master decided by MaxScale)
 *
 * @param test Tester object
 * @return Master server id
 */
int get_server_id(TestConnections& test)
{
    MYSQL *conn = test.maxscales->open_rwsplit_connection(0);
    int id = -1;
    char str[1024];

    if (find_field(conn, "SELECT @@server_id, @@last_insert_id;", "@@server_id", str) == 0)
    {
        id = atoi(str);
    }

    mysql_close(conn);
    return id;
}

static bool interactive = false;

void get_input()
{
    if (interactive)
    {
        printf("--- Press any key to confinue ---\n");
        getchar();
    }
}

void fix_replication_create_table(TestConnections& test)
{
    test.tprintf("Fix replication and recreate table.");
    test.maxscales->close_maxscale_connections(0);
    test.repl->fix_replication();
    test.maxscales->connect_maxscale(0);
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE OR REPLACE TABLE test.t1(id INT)");
    test.repl->sync_slaves();
    inserts = 0;

    check(test);
    get_output(test);
}

int main(int argc, char** argv)
{
    const char* LINE = "------------------------------------------";
    const char* PRINT_ID = "Master server id is %d.";
    const char* WRONG_SLAVE = "Wrong slave was promoted or promotion failed.";

    interactive = strcmp(argv[argc - 1], "interactive") == 0;
    int master_id = -1;
    TestConnections test(argc, argv);

    // Wait a few seconds
    sleep(5);

    test.tprintf("Creating table and inserting data.");
    get_input();
    test.maxscales->connect_maxscale(0);
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE OR REPLACE TABLE test.t1(id INT)");
    test.repl->sync_slaves();

    check(test);
    get_output(test);

    // Test 1
    test.tprintf("Test 1: Stopping master and waiting for failover. Check that another server is promoted.\n"
                 "%s", LINE);
    get_input();
    int node0_id = test.repl->get_server_id(0); // Read master id now before shutdown.
    test.repl->stop_node(0);
    sleep(10);

    check(test);
    get_output(test);

    master_id = get_server_id(test);
    test.tprintf(PRINT_ID, master_id);
    test.add_result(master_id < 1 && master_id == node0_id, "Master did not change or no master detected.");
    fix_replication_create_table(test);
    test.repl->connect();

    // Test 2
    test.tprintf("Test 2: Disable replication on server 2 and kill master, check that server 3 or 4 is "
                 "promoted.\n%s", LINE);
    get_input();
    execute_query(test.repl->nodes[1], "STOP SLAVE; RESET SLAVE ALL;");
    sleep(2);
    test.repl->stop_node(0);
    sleep(10);

    check(test);
    get_output(test);

    master_id = get_server_id(test);
    test.tprintf(PRINT_ID, master_id);
    test.add_result(master_id < 1 ||
                    (master_id != test.repl->get_server_id(2) && master_id != test.repl->get_server_id(3)),
                    WRONG_SLAVE);
    fix_replication_create_table(test);
    test.repl->connect();


    // Test 3
    test.tprintf("Test3: Shutdown two slaves (servers 2 and 4). Disable log_bin on server 2, making it "
                 "invalid for promotion. Enable log-slave-updates on servers 2 and 4. Check that server 4 is "
                 "promoted on master failure.\n%s", LINE);
    get_input();

    test.repl->stop_node(1);
    test.repl->stop_node(3);
    test.repl->stash_server_settings(1);
    test.repl->stash_server_settings(3);
    test.repl->disable_server_setting(1, "log-bin");
    const char* log_slave = "log_slave_updates=1";
    test.repl->add_server_setting(1, log_slave);
    test.repl->add_server_setting(3, log_slave);
    test.repl->start_node(1, (char *) "");
    test.repl->start_node(3, (char *) "");
    sleep(4);
    test.tprintf("Settings changed.");
    get_output(test);
    test.tprintf("Stopping master.");
    test.repl->stop_node(0);
    sleep(10);

    check(test);
    get_output(test);

    master_id = get_server_id(test);
    // Because servers have been restarted, redo connections.
    test.repl->connect();
    sleep(2);
    test.tprintf(PRINT_ID, master_id);
    test.add_result(master_id < 1 || master_id != test.repl->get_server_id(3), WRONG_SLAVE);
    // Restore server 2 and 4 settings. Because server 4 is now the master, shutting it down causes
    // another failover. Prevent this by stopping maxscale.
    test.tprintf("Restoring server settings.");
    test.maxscales->stop_maxscale(0);
    test.repl->stop_node(1);
    test.repl->stop_node(3);
    sleep(4);
    test.repl->restore_server_settings(1);
    test.repl->restore_server_settings(3);
    test.repl->start_node(0, (char *) "");
    test.repl->start_node(1, (char *) "");
    test.repl->start_node(3, (char *) "");
    sleep(4);
    test.maxscales->start_maxscale(0);
    sleep(2);
    get_output(test);
    get_input();

    test.repl->fix_replication();
    return test.global_result;
}
