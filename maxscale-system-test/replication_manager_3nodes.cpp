/**
 * Test replication-manager - Three node setup
 */

#include "testconnections.h"
#include <termios.h>

void prepare(TestConnections& test)
{
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_iflag |= IGNBRK;
    t.c_iflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);

    test.ssh_maxscale(true, "pcs resource disable maxscale-clone; pcs resource disable replication-manager");

    test.repl->fix_replication();
    system("./manage_mrm.sh configure 3");
    test.copy_from_maxscale((char*)"/etc/maxscale.cnf", (char*)".");
    test.copy_to_maxscale("./config.toml", "~");
    test.ssh_maxscale(false, "sudo cp ~/maxscale.cnf /etc/; sudo cp ~/config.toml /etc/replication-manager/");

    system("sed -i 's/version_string=.*/version_string=10.1.19-maxscale-standby/' ./maxscale.cnf");
    test.galera->copy_to_node("./maxscale.cnf", "~", 0);
    test.galera->copy_to_node("./config.toml", "~", 0);
    test.galera->ssh_node(0, "sudo cp ~/config.toml /etc/replication-manager", false);
    test.galera->ssh_node(0, "sudo cp ~/maxscale.cnf /etc/", false);
    test.ssh_maxscale(true, "replication-manager bootstrap --clean-all;pcs resource enable maxscale-clone; pcs resource enable replication-manager");
    sleep(5);
}

void get_output(TestConnections& test)
{
    test.tprintf("Maxadmin output:");
    char *output = test.ssh_maxscale_output(true, "maxadmin list servers");
    test.tprintf("%s", output);
    free(output);
}

static int inserts = 0;

void check(TestConnections& test)
{
    MYSQL *conn = test.open_rwsplit_connection();
    const char *query1 = "INSERT INTO test.t1 VALUES (%d)";
    const char *query2 = "SELECT * FROM test.t1";

    printf("\nExecuting queries though MaxScale:\n\n");
    printf("BEGIN\n");
    test.try_query(conn, "BEGIN");
    printf(query1, inserts);
    printf("\n");
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

        printf("%s\n%s\n", query2, values.c_str());
        mysql_free_result(res);
    }

    mysql_query(conn, "SELECT @@server_id, @@hostname");
    res = mysql_store_result(conn);
    if (res)
    {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)))
        {
            printf("SELECT @@server_id, @@hostname\n%s, %s\n", row[0], row[1]);
        }
        mysql_free_result(res);
    }

    printf("COMMIT\n");
    test.try_query(conn, "COMMIT");

    mysql_query(conn, "SELECT @@server_id, @@hostname");
    res = mysql_store_result(conn);
    if (res)
    {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)))
        {
            printf("SELECT @@server_id, @@hostname\n%s, %s\n", row[0], row[1]);
        }
        mysql_free_result(res);
    }
    printf("\n");

    get_output(test);

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

void do_sleep(int s)
{
    printf("Waiting for %d seconds.", s);
    fflush(stdout);

    for (int i = 0; i < s; i++)
    {
        printf(".");
        fflush(stdout);
        sleep(1);
    }

    printf(" Done!\n");
    fflush(stdout);
}

int main(int argc, char** argv)
{
    interactive = strcmp(argv[argc - 1], "interactive") == 0;
    TestConnections::check_nodes(false);
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);
    prepare(test);

    test.tprintf("Creating table and inserting data");
    get_input();
    test.connect_maxscale();
    test.try_query(test.conn_rwsplit, "CREATE OR REPLACE TABLE test.t1(id INT)");
    test.close_maxscale_connections();

    check(test);

    // test.tprintf("Stopping master and waiting for it to fail over");
    // get_input();
    // test.repl->stop_node(0);
    // do_sleep(15);

    // check(test);

    // test.tprintf("Restarting old master");
    // get_input();
    // test.repl->start_node(0, (char*)"");
    // do_sleep(15);

    // check(test);

    test.tprintf("Stopping the first slave");
    get_input();
    test.repl->stop_node(1);
    do_sleep(15);

    check(test);


    test.tprintf("Stopping the second slave");
    get_input();
    test.repl->stop_node(2);
    do_sleep(15);

    check(test);

    test.tprintf("Restarting the second slave");
    get_input();
    test.repl->start_node(2, (char*)"");
    do_sleep(15);

    check(test);

    test.tprintf("Stopping the master and waiting for it to fail over");
    get_input();
    test.repl->stop_node(0);
    do_sleep(15);

    check(test);

    // test.tprintf("Stopping first slave, the second slave is promoted as the master");
    // get_input();
    // test.repl->stop_node(1);
    // do_sleep(15);

    // check(test);

    // test.tprintf("Restarting the first slave");
    // get_input();
    test.repl->start_node(1, (char*)"");
    // do_sleep(15);

    // check(test);

    // test.tprintf("Restarting the original master");
    // get_input();
    test.repl->start_node(0, (char*)"");
    do_sleep(5);

    // check(test);

    test.connect_maxscale();
    test.try_query(test.conn_rwsplit, "DROP TABLE test.t1");
    test.close_maxscale_connections();

    return test.global_result;
}
