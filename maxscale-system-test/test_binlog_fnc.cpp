#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

#include "test_binlog_fnc.h"

int check_sha1(TestConnections* Test)
{
    char sys[1024];
    char * x;
    int local_result = 0;
    int i;
    int exit_code;

    char *s_maxscale;
    char *s;

    Test->set_timeout(50);
    Test->tprintf("ls before FLUSH LOGS\n");
    Test->tprintf("Maxscale\n");
    Test->ssh_maxscale(true, "ls -la %s/mar-bin.0000*", Test->maxscale_binlog_dir);
    Test->tprintf("Master\n");
    Test->set_timeout(50);
    Test->ssh_maxscale(false, "ls -la /var/lib/mysql/mar-bin.0000*");

    Test->tprintf("FLUSH LOGS\n");
    Test->set_timeout(100);
    local_result += execute_query(Test->repl->nodes[0], (char *) "FLUSH LOGS");
    Test->tprintf("Logs flushed\n");
    Test->set_timeout(100);
    sleep(20);
    Test->tprintf("ls after first FLUSH LOGS\n");
    Test->tprintf("Maxscale\n");
    Test->set_timeout(50);
    Test->ssh_maxscale(true, "ls -la %s/mar-bin.0000*", Test->maxscale_binlog_dir);

    Test->tprintf("Master\n");
    Test->set_timeout(50);
    Test->ssh_maxscale(false, "ls -la /var/lib/mysql/mar-bin.0000*");

    Test->set_timeout(100);
    Test->tprintf("FLUSH LOGS\n");
    local_result += execute_query(Test->repl->nodes[0], (char *) "FLUSH LOGS");
    Test->tprintf("Logs flushed\n");

    Test->set_timeout(50);
    sleep(20);
    Test->set_timeout(50);
    Test->tprintf("ls before FLUSH LOGS\n");
    Test->tprintf("Maxscale\n");

    Test->ssh_maxscale(true, "ls -la %s/mar-bin.0000*", Test->maxscale_binlog_dir);

    Test->tprintf("Master\n");
    Test->set_timeout(50);
    Test->ssh_maxscale(false, "ls -la /var/lib/mysql/mar-bin.0000*");


    for (i = 1; i < 3; i++)
    {
        Test->tprintf("\nFILE: 000000%d\n", i);
        Test->set_timeout(50);
        s_maxscale = Test->ssh_maxscale_output(true, "sha1sum %s/mar-bin.00000%d", Test->maxscale_binlog_dir, i);
        if (s_maxscale != NULL)
        {
            x = strchr(s_maxscale, ' ');
            if (x != NULL )
            {
                x[0] = 0;
            }
            Test->tprintf("Binlog checksum from Maxscale %s\n", s_maxscale);
        }

        sprintf(sys, "sha1sum /var/lib/mysql/mar-bin.00000%d", i);
        Test->set_timeout(50);
        s = Test->repl->ssh_node_output(0, sys, true, &exit_code);
        if (s != NULL)
        {
            x = strchr(s, ' ');
            if (x != NULL )
            {
                x[0] = 0;
            }
            Test->tprintf("Binlog checksum from master %s\n", s);
        }
        if (strcmp(s_maxscale, s) != 0)
        {
            Test->tprintf("Binlog from master checksum is not eqiual to binlog checksum from Maxscale node\n");
            local_result++;
        }
    }
    return local_result;
}

int start_transaction(TestConnections* Test)
{
    int local_result = 0;
    Test->tprintf("Transaction test\n");
    Test->tprintf("Start transaction\n");
    execute_query(Test->repl->nodes[0], (char *) "DELETE FROM t1 WHERE fl=10;");
    local_result += execute_query(Test->repl->nodes[0], (char *) "START TRANSACTION");
    local_result += execute_query(Test->repl->nodes[0], (char *) "SET autocommit = 0");
    Test->tprintf("INSERT data\n");
    local_result += execute_query(Test->repl->nodes[0], (char *) "INSERT INTO t1 VALUES(111, 10)");
    Test->stop_timeout();
    sleep(20);
    return local_result;
}

void test_binlog(TestConnections* Test)
{
    int i;
    MYSQL* binlog;
    Test->repl->connect();

    Test->set_timeout(100);
    Test->try_query(Test->repl->nodes[0], (char *) "SET NAMES utf8mb4");
    Test->try_query(Test->repl->nodes[0], (char *) "set autocommit=1");
    Test->try_query(Test->repl->nodes[0], (char *) "select USER()");

    Test->set_timeout(100);
    create_t1(Test->repl->nodes[0]);
    Test->add_result(insert_into_t1(Test->repl->nodes[0], 4), "Data inserting to t1 failed\n");
    Test->stop_timeout();
    Test->tprintf("Sleeping to let replication happen\n");
    sleep(60);

    for (i = 0; i < Test->repl->N; i++)
    {
        Test->tprintf("Checking data from node %d (%s)\n", i, Test->repl->IP[i]);
        Test->set_timeout(100);
        Test->add_result(select_from_t1(Test->repl->nodes[i], 4), "Selecting from t1 failed\n");
        Test->stop_timeout();
    }

    Test->set_timeout(10);
    Test->tprintf("First transaction test (with ROLLBACK)\n");
    start_transaction(Test);

    Test->set_timeout(50);

    Test->tprintf("SELECT * FROM t1 WHERE fl=10, checking inserted values\n");
    Test->add_result(execute_query_check_one(Test->repl->nodes[0], (char *) "SELECT * FROM t1 WHERE fl=10",
                     "111"), "SELECT check failed\n");

    //Test->add_result(check_sha1(Test), "sha1 check failed\n");

    Test->tprintf("ROLLBACK\n");
    Test->try_query(Test->repl->nodes[0], (char *) "ROLLBACK");
    Test->tprintf("INSERT INTO t1 VALUES(112, 10)\n");
    Test->try_query(Test->repl->nodes[0], (char *) "INSERT INTO t1 VALUES(112, 10)");
    Test->try_query(Test->repl->nodes[0], (char *) "COMMIT");
    Test->stop_timeout();
    sleep(20);

    Test->set_timeout(20);
    Test->tprintf("SELECT * FROM t1 WHERE fl=10, checking inserted values\n");
    Test->add_result(execute_query_check_one(Test->repl->nodes[0], (char *) "SELECT * FROM t1 WHERE fl=10",
                     "112"), "SELECT check failed\n");

    Test->tprintf("SELECT * FROM t1 WHERE fl=10, checking inserted values from slave\n");
    Test->add_result(execute_query_check_one(Test->repl->nodes[2], (char *) "SELECT * FROM t1 WHERE fl=10",
                     "112"), "SELECT check failed\n");
    Test->tprintf("DELETE FROM t1 WHERE fl=10\n");
    Test->try_query(Test->repl->nodes[0], (char *) "DELETE FROM t1 WHERE fl=10");
    Test->tprintf("Checking t1\n");
    Test->add_result(select_from_t1(Test->repl->nodes[0], 4), "SELECT from t1 failed\n");

    Test->tprintf("Second transaction test (with COMMIT)\n");
    start_transaction(Test);

    Test->tprintf("COMMIT\n");
    Test->try_query(Test->repl->nodes[0], (char *) "COMMIT");

    Test->tprintf("SELECT, checking inserted values\n");
    Test->add_result(execute_query_check_one(Test->repl->nodes[0], (char *) "SELECT * FROM t1 WHERE fl=10",
                     "111"), "SELECT check failed\n");

    Test->tprintf("SELECT, checking inserted values from slave\n");
    Test->add_result(execute_query_check_one(Test->repl->nodes[2], (char *) "SELECT * FROM t1 WHERE fl=10",
                     "111"), "SELECT check failed\n");
    Test->tprintf("DELETE FROM t1 WHERE fl=10\n");
    Test->try_query(Test->repl->nodes[0], (char *) "DELETE FROM t1 WHERE fl=10");

    Test->stop_timeout();

    Test->set_timeout(50);
    Test->add_result(check_sha1(Test), "sha1 check failed\n");
    Test->repl->close_connections();

    Test->stop_timeout();

    // test SLAVE STOP/START
    for (int j = 0; j < 3; j++)
    {
        Test->set_timeout(100);
        Test->repl->connect();

        Test->tprintf("Dropping and re-creating t1\n");
        Test->try_query(Test->repl->nodes[0], (char *) "DROP TABLE IF EXISTS t1");
        create_t1(Test->repl->nodes[0]);

        Test->tprintf("Connecting to MaxScale binlog router\n");
        binlog = open_conn(Test->binlog_port, Test->maxscale_IP, Test->repl->user_name, Test->repl->password,
                           Test->ssl);

        Test->tprintf("STOP SLAVE against Maxscale binlog\n");
        execute_query(binlog, (char *) "STOP SLAVE");

        if (j == 1)
        {
            Test->tprintf("FLUSH LOGS on master\n");
            execute_query(Test->repl->nodes[0], (char *) "FLUSH LOGS");
        }
        Test->add_result(insert_into_t1(Test->repl->nodes[0], 4), "INSERT into t1 failed\n");

        Test->tprintf("START SLAVE against Maxscale binlog\n");
        Test->try_query(binlog, (char *) "START SLAVE");

        Test->tprintf("Sleeping to let replication happen\n");
        Test->stop_timeout();
        sleep(30);

        for (i = 0; i < Test->repl->N; i++)
        {
            Test->set_timeout(50);
            Test->tprintf("Checking data from node %d (%s)\n", i, Test->repl->IP[i]);
            Test->add_result(select_from_t1(Test->repl->nodes[i], 4), "SELECT from t1 failed\n");
        }

        Test->set_timeout(100);
        Test->add_result(check_sha1(Test), "sha1 check failed\n");
        Test->repl->close_connections();
        Test->stop_timeout();
    }
}

