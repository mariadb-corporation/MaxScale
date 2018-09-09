#include "testconnections.h"

int inserts = 0;
bool interactive = false;

const char LINE[] = "------------------------------------------";
const char PRINT_ID[] = "Master server id is %d.";
const char WRONG_SLAVE[] = "Wrong slave was promoted or promotion failed.";
const char GTID_QUERY[] = "SELECT @@gtid_current_pos;";
const char GTID_FIELD[] = "@@gtid_current_pos";
const int bufsize = 512;

void get_output(TestConnections& test)
{
    int ec;
    test.tprintf("Maxadmin output:");
    char* output = test.maxscales->ssh_node_output(0, "maxadmin list servers", true, &ec);
    test.tprintf("%s", output);
    free(output);

    if (test.verbose)
    {
        test.tprintf("MaxScale output:");
    }
    output = test.maxscales->ssh_node_output(0,
                                             "cat /var/log/maxscale/maxscale.log && "
                                             "sudo truncate -s 0 /var/log/maxscale/maxscale.log",
                                             true,
                                             &ec);
    if (test.verbose)
    {
        test.tprintf("%s", output);
    }
    free(output);
}

void check(TestConnections& test)
{
    MYSQL* conn = test.maxscales->open_rwsplit_connection(0);
    const char* query1 = "INSERT INTO test.t1 VALUES (%d)";
    const char* query2 = "SELECT * FROM test.t1";

    test.try_query(conn, "BEGIN");
    test.tprintf(query1, inserts);
    test.try_query(conn, query1, inserts++);
    mysql_query(conn, query2);

    MYSQL_RES* res = mysql_store_result(conn);
    test.add_result(res == NULL, "Query should return a result set");

    if (res)
    {
        std::string values;
        MYSQL_ROW row;
        int num_rows = mysql_num_rows(res);
        test.add_result(num_rows != inserts,
                        "Query returned %d rows when %d rows were expected",
                        num_rows,
                        inserts);
        const char* separator = "";

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
int get_master_server_id(TestConnections& test)
{
    MYSQL* conn = test.maxscales->open_rwsplit_connection(0);
    int id = -1;
    char str[1024];

    if (find_field(conn, "SELECT @@server_id, @@last_insert_id;", "@@server_id", str) == 0)
    {
        id = atoi(str);
    }

    mysql_close(conn);
    return id;
}

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

void delete_slave_binlogs(TestConnections& test)
{
    const char RESET[] = "RESET MASTER;";
    execute_query(test.repl->nodes[0], "SET GLOBAL gtid_slave_pos='0-1-0';");
    execute_query(test.repl->nodes[1], RESET);
    execute_query(test.repl->nodes[2], RESET);
    execute_query(test.repl->nodes[3], RESET);
}

void basic_test(TestConnections& test)
{
    test.tprintf("Creating table and inserting data.");
    get_input();
    test.maxscales->connect_maxscale(0);
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE OR REPLACE TABLE test.t1(id INT)");
    test.repl->sync_slaves();

    check(test);
    get_output(test);
}

/**
 * Do inserts, check that results are as expected.
 *
 * @param test Test connections
 * @param conn Which specific connection to use
 * @param insert_count How many inserts should be done
 * @return True, if successful
 */
bool generate_traffic_and_check(TestConnections& test, MYSQL* conn, int insert_count)
{
    const char INSERT[] = "INSERT INTO test.t1 VALUES (%d);";
    const char SELECT[] = "SELECT * FROM test.t1 ORDER BY id ASC;";
    timespec short_sleep;
    short_sleep.tv_sec = 0;
    short_sleep.tv_nsec = 100000000;

    mysql_query(conn, "BEGIN");

    for (int i = 0; i < insert_count; i++)
    {
        test.try_query(conn, INSERT, inserts++);
        nanosleep(&short_sleep, NULL);
    }
    bool rval = false;

    mysql_query(conn, SELECT);
    MYSQL_RES* res = mysql_store_result(conn);
    test.assert(res != NULL, "Query did not return a result set");

    if (res)
    {
        rval = true;
        MYSQL_ROW row;
        // Check all values, they should go from 0 to 'inserts'
        int expected_val = 0;
        while ((row = mysql_fetch_row(res)))
        {
            int value_read = strtol(row[0], NULL, 0);
            if (value_read != expected_val)
            {
                test.assert(false, "Query returned %d when %d was expected", value_read, expected_val);
                rval = false;
                break;
            }
            expected_val++;
        }
        int num_rows = expected_val;
        test.assert(num_rows == inserts,
                    "Query returned %d rows when %d rows were expected",
                    num_rows,
                    inserts);
        if (num_rows != inserts)
        {
            rval = false;
        }
        mysql_free_result(res);
    }
    mysql_query(conn, "COMMIT");
    return rval;
}

void print_gtids(TestConnections& test)
{
    MYSQL* maxconn = test.maxscales->open_rwsplit_connection(0);
    if (maxconn)
    {
        char result_tmp[bufsize];
        if (find_field(maxconn, GTID_QUERY, GTID_FIELD, result_tmp) == 0)
        {
            test.tprintf("MaxScale gtid: %s", result_tmp);
        }
    }
    mysql_close(maxconn);
    test.repl->connect();
    for (int i = 0; i < test.repl->N; i++)
    {
        char result_tmp[bufsize];
        if (find_field(test.repl->nodes[i], GTID_QUERY, GTID_FIELD, result_tmp) == 0)
        {
            test.tprintf("Node %d gtid: %s", i, result_tmp);
        }
    }
}
