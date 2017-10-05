/**
 * Test replication-manager
 */

#include "testconnections.h"
#include <termios.h>

void prepare()
{
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_iflag |= IGNBRK;
    t.c_iflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

void get_output(TestConnections& test)
{
    test.tprintf("Maxadmin output:");
    char *output = test.ssh_maxscale_output(true, "maxadmin list servers");
    test.tprintf("%s", output);
    free(output);

    test.tprintf("replication-manager output:");
    output = test.ssh_maxscale_output(true,
                                      "cat /var/log/replication-manager.log && sudo truncate -s 0 /var/log/replication-manager.log");
    test.tprintf("%s", output);
    free(output);
}

static int inserts = 0;

void check(TestConnections& test)
{
    MYSQL *conn = test.open_rwsplit_connection();
    const char *query1 = "INSERT INTO test.t1 VALUES (%d)";
    const char *query2 = "SELECT * FROM test.t1";

    test.try_query(conn, "BEGIN");
    test.tprintf(query1, inserts);
    test.try_query(conn, query1, inserts++);
    mysql_query(conn, query2);

    MYSQL_RES *res = mysql_store_result(conn);
    test.add_result(res == NULL, "Query shoud return a result set");

    if (res)
    {
        std::string values;
        MYSQL_ROW row;
        int num_rows = mysql_num_rows(res);
        test.add_result(num_rows != inserts, "Query returned %d rows when %d rows were expected", num_rows, inserts);
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

static bool interactive = false;

void get_input()
{
    if (interactive)
    {
        printf("--- Press any key to confinue ---\n");
        getchar();
    }
}

int main(int argc, char** argv)
{
    interactive = strcmp(argv[argc - 1], "interactive") == 0;
    prepare();

    TestConnections test(argc, argv);
    test.tprintf("Installing replication-manager");
    int rc = system("./manage_mrm.sh install > manage_mrm.log");
    if (!WIFEXITED(rc) || WEXITSTATUS(rc) != 0)
    {
        test.tprintf("Failed to install replication-manager, see manage_mrm.log for more details");
        return -1;
    }

    // Wait a few seconds
    sleep(5);

    test.tprintf("Creating table and inserting data");
    get_input();
    test.connect_maxscale();
    test.try_query(test.maxscales->conn_rwsplit[0], "CREATE OR REPLACE TABLE test.t1(id INT)");

    check(test);
    get_output(test);

    test.tprintf("Stopping master and waiting for it to fail over");
    get_input();
    test.repl->stop_node(0);
    sleep(10);

    check(test);
    get_output(test);

    test.tprintf("Stopping another node and waiting for replication-manager to detect it");
    get_input();
    test.repl->stop_node(1);
    sleep(10);

    check(test);
    get_output(test);
    get_input();

    test.tprintf("Stopping all but one node and waiting for replication-manager to detect it");
    get_input();
    test.repl->stop_node(2);
    sleep(10);

    check(test);
    get_output(test);

    test.tprintf("Starting all nodes and wait for replication-manager to fix the replication");
    get_input();

    test.repl->start_node(0, (char*)"");
    sleep(5);
    test.repl->start_node(1, (char*)"");
    sleep(5);
    test.repl->start_node(2, (char*)"");
    sleep(5);

    check(test);
    get_output(test);

    test.tprintf("Dropping tables");
    get_input();
    test.close_maxscale_connections();
    test.connect_maxscale();
    test.try_query(test.maxscales->conn_rwsplit[0], "DROP TABLE test.t1");
    test.close_maxscale_connections();

    get_output(test);

    test.tprintf("Removing replication-manager");
    get_input();
    system("./manage_mrm.sh remove >> manage_mrm.log");
    test.repl->fix_replication();
    return test.global_result;
}
