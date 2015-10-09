#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

#include "test_binlog_fnc.h"

int check_sha1(TestConnections* Test)
{
    char sys[1024];
    char * x;
    FILE *ls;
    int global_result = 0;
    int i;

    char buf[1024];
    char buf_max[1024];

    Test->tprintf("ls before FLUSH LOGS\n");
    Test->tprintf("Maxscale");
    sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s 'ls -la %s/mar-bin.0000*'",
            Test->maxscale_sshkey, Test->maxscale_access_user, Test->maxscale_IP, Test->maxscale_binlog_dir);
    system(sys);
    Test->tprintf("Master");fflush(stdout);
    sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s 'ls -la /var/lib/mysql/mar-bin.0000*'",
            Test->repl->sshkey[0], Test->repl->access_user[0], Test->repl->IP[0]);
    system(sys);

    printf("FLUSH LOGS\n");fflush(stdout);
    global_result += execute_query(Test->repl->nodes[0], (char *) "FLUSH LOGS");
    Test->tprintf("Logs flushed\n");
    sleep(20);
    Test->tprintf("ls after first FLUSH LOGS\n");
    Test->tprintf("Maxscale\n");fflush(stdout);
    sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s 'ls -la %s/mar-bin.0000*'",
            Test->maxscale_sshkey, Test->maxscale_access_user, Test->maxscale_IP, Test->maxscale_binlog_dir);
    system(sys);
    Test->tprintf("Master\n");
    sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s 'ls -la /var/lib/mysql/mar-bin.00000*'",
            Test->repl->sshkey[0], Test->repl->access_user[0], Test->repl->IP[0]);
    system(sys);


    Test->tprintf("FLUSH LOGS\n");
    global_result += execute_query(Test->repl->nodes[0], (char *) "FLUSH LOGS");
    Test->tprintf("Logs flushed\n");

    sleep(19);
    Test->tprintf("ls before FLUSH LOGS\n");
    Test->tprintf("Maxscale");
    sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s 'ls -la %s/mar-bin.0000*'",
            Test->maxscale_sshkey, Test->maxscale_access_user, Test->maxscale_IP, Test->maxscale_binlog_dir);
    system(sys);
    Test->tprintf("Master");
    sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s 'ls -la /var/lib/mysql/mar-bin.00000*'",
            Test->repl->sshkey[0], Test->repl->access_user[0], Test->repl->IP[0]);
    system(sys);fflush(stdout);

    for (i = 1; i < 3; i++) {
        Test->tprintf("\nFILE: 000000%d\n", i);
        sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s '%s sha1sum %s/mar-bin.00000%d'",
                Test->maxscale_sshkey, Test->maxscale_access_user, Test->maxscale_IP, Test->maxscale_access_sudo, Test->maxscale_binlog_dir, i);
        ls = popen(sys, "r");
        fgets(buf_max, sizeof(buf), ls);
        pclose(ls);
        x = strchr(buf_max, ' '); x[0] = 0;
        Test->tprintf("Binlog checksum from Maxscale %s\n", buf_max);

        sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s '%s sha1sum /var/lib/mysql/mar-bin.00000%d'",
                Test->repl->sshkey[0], Test->repl->access_user[0], Test->repl->IP[0], Test->repl->access_sudo[0], i);
        ls = popen(sys, "r");
        fgets(buf, sizeof(buf), ls);
        pclose(ls);
        x = strchr(buf, ' '); x[0] = 0;
        Test->tprintf("Binlog checksum from master %s\n", buf);
        if (strcmp(buf_max, buf) != 0) {
            Test->tprintf("Binlog from master checksum is not eqiual to binlog checksum from Maxscale node\n");
            global_result++;
        }
    }
    return(global_result);
}

int start_transaction(TestConnections* Test)
{
    int global_result = 0;
    Test->tprintf("Transaction test\n");
    Test->tprintf("Start transaction\n");
    global_result += execute_query(Test->repl->nodes[0], (char *) "START TRANSACTION");
    global_result += execute_query(Test->repl->nodes[0], (char *) "SET autocommit = 0");
    Test->tprintf("INSERT data\n");
    global_result += execute_query(Test->repl->nodes[0], (char *) "INSERT INTO t1 VALUES(111, 10)");
    sleep(20);
    return(global_result);
}

int test_binlog(TestConnections* Test)
{
    int i;
    MYSQL* binlog;
    int global_result = 0;
    Test->repl->connect();

    Test->set_timeout(100);
    create_t1(Test->repl->nodes[0]);
    global_result += insert_into_t1(Test->repl->nodes[0], 4);
    Test->stop_timeout();
    Test->tprintf("Sleeping to let replication happen\n"); fflush(stdout);
    sleep(30);

    for (i = 0; i < Test->repl->N; i++) {
        Test->tprintf("Checking data from node %d (%s)\n", i, Test->repl->IP[i]); fflush(stdout);
        Test->set_timeout(100);
        global_result += select_from_t1(Test->repl->nodes[i], 4);
        Test->stop_timeout();
    }

    Test->set_timeout(10);
    Test->tprintf("First transaction test (with ROLLBACK)\n");
    start_transaction(Test);
    Test->stop_timeout();

    Test->set_timeout(10);

    Test->tprintf("SELECT * FROM t1 WHERE fl=10, checking inserted values\n");
    global_result += execute_query_check_one(Test->repl->nodes[0], (char *) "SELECT * FROM t1 WHERE fl=10", "111");

    //printf("SELECT, checking inserted values from slave\n");
    //global_result += execute_query_check_one(Test->repl->nodes[2], (char *) "SELECT * FROM t1 WHERE fl=10", "111");

    global_result += check_sha1(Test);

    Test->tprintf("ROLLBACK\n");
    global_result += execute_query(Test->repl->nodes[0], (char *) "ROLLBACK");
    Test->tprintf("INSERT INTO t1 VALUES(112, 10)\n");
    global_result += execute_query(Test->repl->nodes[0], (char *) "INSERT INTO t1 VALUES(112, 10)");
    Test->stop_timeout();
    sleep(20);

    Test->set_timeout(10);
    Test->tprintf("SELECT * FROM t1 WHERE fl=10, checking inserted values\n");
    global_result += execute_query_check_one(Test->repl->nodes[0], (char *) "SELECT * FROM t1 WHERE fl=10", "112");

    Test->tprintf("SELECT * FROM t1 WHERE fl=10, checking inserted values from slave\n");
    global_result += execute_query_check_one(Test->repl->nodes[2], (char *) "SELECT * FROM t1 WHERE fl=10", "112");
    Test->tprintf("DELETE FROM t1 WHERE fl=10\n");
    global_result += execute_query(Test->repl->nodes[0], (char *) "DELETE FROM t1 WHERE fl=10");
    Test->tprintf("Checking t1\n");
    global_result += select_from_t1(Test->repl->nodes[0], 4);

    Test->tprintf("Second transaction test (with COMMIT)\n");
    start_transaction(Test);

    Test->tprintf("COMMIT\n");
    global_result += execute_query(Test->repl->nodes[0], (char *) "COMMIT");

    Test->tprintf("SELECT, checking inserted values\n");
    global_result += execute_query_check_one(Test->repl->nodes[0], (char *) "SELECT * FROM t1 WHERE fl=10", "111");

    Test->tprintf("SELECT, checking inserted values from slave\n");
    global_result += execute_query_check_one(Test->repl->nodes[2], (char *) "SELECT * FROM t1 WHERE fl=10", "111");
    Test->tprintf("DELETE FROM t1 WHERE fl=10\n");
    global_result += execute_query(Test->repl->nodes[0], (char *) "DELETE FROM t1 WHERE fl=10");

    Test->stop_timeout();

    Test->set_timeout(20);
    global_result += check_sha1(Test);
    Test->repl->close_connections();

    Test->stop_timeout();

    // test SLAVE STOP/START
    for (int j = 0; j < 3; j++) {
        Test->set_timeout(100);
        Test->repl->connect();

        Test->tprintf("Dropping and re-creating t1");
        execute_query(Test->repl->nodes[0], (char *) "DROP TABLE IF EXISTS t1");
        create_t1(Test->repl->nodes[0]);

        Test->tprintf("Connecting to MaxScale binlog router\n");
        binlog = open_conn(Test->binlog_port, Test->maxscale_IP, Test->repl->user_name, Test->repl->password, Test->ssl);

        Test->tprintf("STOP SLAVE against Maxscale binlog");
        execute_query(binlog, (char *) "STOP SLAVE");

        if (j == 1) {
            Test->tprintf("FLUSH LOGS on master");
            execute_query(Test->repl->nodes[0], (char *) "FLUSH LOGS");
        }
        global_result += insert_into_t1(Test->repl->nodes[0], 4);


        Test->tprintf("START SLAVE against Maxscale binlog"); fflush(stdout);
        execute_query(binlog, (char *) "START SLAVE");

        Test->tprintf("Sleeping to let replication happen\n"); fflush(stdout);
        sleep(30);

        for (i = 0; i < Test->repl->N; i++) {
            Test->tprintf("Checking data from node %d (%s)\n", i, Test->repl->IP[i]); fflush(stdout);
            global_result += select_from_t1(Test->repl->nodes[i], 4);
        }

        global_result += check_sha1(Test);
        Test->repl->close_connections();
        Test->stop_timeout();
    }
    return(global_result);
}

