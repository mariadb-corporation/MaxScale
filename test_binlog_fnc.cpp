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

    printf("ls before FLUSH LOGS\n");
    printf("Maxscale");fflush(stdout);
    sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s 'ls -la %s/mar-bin.0000*'",
            Test->maxscale_sshkey, Test->maxscale_access_user, Test->maxscale_IP, Test->maxscale_binlog_dir);
    system(sys);
    printf("Master");fflush(stdout);
    sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s 'ls -la /var/lib/mysql/mar-bin.0000*'",
            Test->repl->sshkey[0], Test->repl->access_user[0], Test->repl->IP[0]);
    system(sys);

    printf("FLUSH LOGS\n");fflush(stdout);
    global_result += execute_query(Test->repl->nodes[0], (char *) "FLUSH LOGS");
    printf("Logs flushed\n");
    sleep(20);
    printf("ls after first FLUSH LOGS\n");
    printf("Maxscale\n");fflush(stdout);
    sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s 'ls -la %s/mar-bin.0000*'",
            Test->maxscale_sshkey, Test->maxscale_access_user, Test->maxscale_IP, Test->maxscale_binlog_dir);
    system(sys);
    printf("Master\n");fflush(stdout);
    sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s 'ls -la /var/lib/mysql/mar-bin.00000*'",
            Test->repl->sshkey[0], Test->repl->access_user[0], Test->repl->IP[0]);
    system(sys);


    printf("FLUSH LOGS\n");fflush(stdout);
    global_result += execute_query(Test->repl->nodes[0], (char *) "FLUSH LOGS");
    printf("Logs flushed\n"); fflush(stdout);

    sleep(19);
    printf("ls before FLUSH LOGS\n");
    printf("Maxscale");
    sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s 'ls -la %s/mar-bin.0000*'",
            Test->maxscale_sshkey, Test->maxscale_access_user, Test->maxscale_IP, Test->maxscale_binlog_dir);
    system(sys);
    printf("Master");
    sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s 'ls -la /var/lib/mysql/mar-bin.00000*'",
            Test->repl->sshkey[0], Test->repl->access_user[0], Test->repl->IP[0]);
    system(sys);fflush(stdout);

    for (i = 1; i < 3; i++) {
        printf("\nFILE: 000000%d\n", i);
        sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s '%s sha1sum %s/mar-bin.00000%d'",
                Test->maxscale_sshkey, Test->maxscale_access_user, Test->maxscale_IP, Test->maxscale_access_sudo, Test->maxscale_binlog_dir, i);
        ls = popen(sys, "r");
        fgets(buf_max, sizeof(buf), ls);
        pclose(ls);
        x = strchr(buf_max, ' '); x[0] = 0;
        printf("Binlog checksum from Maxscale %s\n", buf_max);


        sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null %s@%s '%s sha1sum /var/lib/mysql/mar-bin.00000%d'",
                Test->repl->sshkey[0], Test->repl->access_user[0], Test->repl->IP[0], Test->repl->access_sudo[0], i);
        ls = popen(sys, "r");
        fgets(buf, sizeof(buf), ls);
        pclose(ls);
        x = strchr(buf, ' '); x[0] = 0;
        printf("Binlog checksum from master %s\n", buf);
        if (strcmp(buf_max, buf) != 0) {
            printf("Binlog from master checksum is not eqiual to binlog checksum from Maxscale node\n");
            global_result++;
        }
    }
    return(Test->global_result);
}

int start_transaction(TestConnections* Test)
{
    int global_result = 0;
    printf("Transaction test\n");
    printf("Start transaction\n");
    global_result += execute_query(Test->repl->nodes[0], (char *) "START TRANSACTION");
    global_result += execute_query(Test->repl->nodes[0], (char *) "SET autocommit = 0");
    printf("INSERT data\n");
    global_result += execute_query(Test->repl->nodes[0], (char *) "INSERT INTO t1 VALUES(111, 10)");
    sleep(20);
    return(Test->global_result);
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
    printf("Sleeping to let replication happen\n"); fflush(stdout);
    sleep(30);

    for (i = 0; i < Test->repl->N; i++) {
        printf("Checking data from node %d (%s)\n", i, Test->repl->IP[i]); fflush(stdout);
        Test->set_timeout(100);
        global_result += select_from_t1(Test->repl->nodes[i], 4);
        Test->stop_timeout();
    }

    Test->set_timeout(10);
    printf("First transaction test (with ROLLBACK)\n");
    start_transaction(Test);
    Test->stop_timeout();

    Test->set_timeout(10);

    printf("SELECT * FROM t1 WHERE fl=10, checking inserted values\n");
    global_result += execute_query_check_one(Test->repl->nodes[0], (char *) "SELECT * FROM t1 WHERE fl=10", "111");

    //printf("SELECT, checking inserted values from slave\n");
    //global_result += execute_query_check_one(Test->repl->nodes[2], (char *) "SELECT * FROM t1 WHERE fl=10", "111");

    global_result += check_sha1(Test);

    printf("ROLLBACK\n");
    global_result += execute_query(Test->repl->nodes[0], (char *) "ROLLBACK");
    printf("INSERT INTO t1 VALUES(112, 10)\n");
    global_result += execute_query(Test->repl->nodes[0], (char *) "INSERT INTO t1 VALUES(112, 10)");
    Test->stop_timeout();
    sleep(20);

    Test->set_timeout(10);
    printf("SELECT * FROM t1 WHERE fl=10, checking inserted values\n");
    global_result += execute_query_check_one(Test->repl->nodes[0], (char *) "SELECT * FROM t1 WHERE fl=10", "112");

    printf("SELECT * FROM t1 WHERE fl=10, checking inserted values from slave\n");
    global_result += execute_query_check_one(Test->repl->nodes[2], (char *) "SELECT * FROM t1 WHERE fl=10", "112");
    printf("DELETE FROM t1 WHERE fl=10\n");
    global_result += execute_query(Test->repl->nodes[0], (char *) "DELETE FROM t1 WHERE fl=10");
    printf("Checking t1\n");
    global_result += select_from_t1(Test->repl->nodes[0], 4);

    printf("Second transaction test (with COMMIT)\n");
    start_transaction(Test);

    printf("COMMIT\n");
    global_result += execute_query(Test->repl->nodes[0], (char *) "COMMIT");

    printf("SELECT, checking inserted values\n");
    global_result += execute_query_check_one(Test->repl->nodes[0], (char *) "SELECT * FROM t1 WHERE fl=10", "111");

    printf("SELECT, checking inserted values from slave\n");
    global_result += execute_query_check_one(Test->repl->nodes[2], (char *) "SELECT * FROM t1 WHERE fl=10", "111");
    printf("DELETE FROM t1 WHERE fl=10\n");
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

        printf("Dropping and re-creating t1"); fflush(stdout);
        execute_query(Test->repl->nodes[0], (char *) "DROP TABLE IF EXISTS t1");
        create_t1(Test->repl->nodes[0]);

        printf("Connecting to MaxScale binlog router\n");fflush(stdout);
        binlog = open_conn(Test->binlog_port, Test->maxscale_IP, Test->repl->user_name, Test->repl->password, Test->ssl);

        printf("STOP SLAVE against Maxscale binlog"); fflush(stdout);
        execute_query(binlog, (char *) "STOP SLAVE");

        if (j == 1) {
            printf("FLUSH LOGS on master");
            execute_query(Test->repl->nodes[0], (char *) "FLUSH LOGS");
        }
        global_result += insert_into_t1(Test->repl->nodes[0], 4);


        printf("START SLAVE against Maxscale binlog"); fflush(stdout);
        execute_query(binlog, (char *) "START SLAVE");

        printf("Sleeping to let replication happen\n"); fflush(stdout);
        sleep(30);

        for (i = 0; i < Test->repl->N; i++) {
            printf("Checking data from node %d (%s)\n", i, Test->repl->IP[i]); fflush(stdout);
            global_result += select_from_t1(Test->repl->nodes[i], 4);
        }

        global_result += check_sha1(Test);
        Test->repl->close_connections();
        Test->stop_timeout();
    }
    return(Test->global_result);
}

