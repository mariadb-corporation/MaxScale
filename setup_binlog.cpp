/**
 * @file setup_binlog.cpp test of simple binlog router setup
 * - setup one master, one slave directly connected to real master and two slaves connected to binlog router
 * - create table and put data into it using connection to master
 * - check data using direct commection to all backend
 * - compare sha1 checksum of binlog file on master and on Maxscale machine
 * - START TRANSACTION
 * - SET autocommit = 0
 * - INSERT INTO t1 VALUES(111, 10)
 * - check SELECT * FROM t1 WHERE fl=10 - expect one row x=111
 * - ROLLBACK
 * - INSERT INTO t1 VALUES(112, 10)
 * - check SELECT * FROM t1 WHERE fl=10 - expect one row x=112 and no row with x=111
 * - DELETE FROM t1 WHERE fl=10
 * - START TRANSACTION
 * - INSERT INTO t1 VALUES(111, 10)
 * - check SELECT * FROM t1 WHERE fl=10 - expect one row x=111 from master and slave
 * - DELETE FROM t1 WHERE fl=10
 * - compare sha1 checksum of binlog file on master and on Maxscale machine
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

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
    sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s 'ls -la %s/Binlog_Service/mar-bin.0000*'", Test->maxscale_sshkey, Test->maxscale_IP, Test->maxdir);
    system(sys);
    printf("Master");fflush(stdout);
    sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s 'ls -la /var/lib/mysql/mar-bin.0000*'", Test->repl->sshkey[0], Test->repl->IP[0]);
    system(sys);

    printf("FLUSH LOGS\n");fflush(stdout);
    global_result += execute_query(Test->repl->nodes[0], (char *) "FLUSH LOGS");
    printf("Logs flushed\n");
    sleep(20);
    printf("ls after first FLUSH LOGS\n");
    printf("Maxscale\n");fflush(stdout);
    sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s 'ls -la %s/Binlog_Service/mar-bin.0000*'", Test->maxscale_sshkey, Test->maxscale_IP, Test->maxdir);
    system(sys);
    printf("Master\n");fflush(stdout);
    sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s 'ls -la /var/lib/mysql/mar-bin.00000*'", Test->repl->sshkey[0], Test->repl->IP[0]);
    system(sys);


    printf("FLUSH LOGS\n");fflush(stdout);
    global_result += execute_query(Test->repl->nodes[0], (char *) "FLUSH LOGS");
    printf("Logs flushed\n"); fflush(stdout);

    sleep(19);
    printf("ls before FLUSH LOGS\n");
    printf("Maxscale");
    sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s 'ls -la %s/Binlog_Service/mar-bin.0000*'", Test->maxscale_sshkey, Test->maxscale_IP, Test->maxdir);
    system(sys);
    printf("Master");
    sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s 'ls -la /var/lib/mysql/mar-bin.00000*'", Test->repl->sshkey[0], Test->repl->IP[0]);
    system(sys);fflush(stdout);

    for (i = 1; i < 3; i++) {
        printf("\nFILE: 000000%d\n", i);
        sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s 'sha1sum %s/Binlog_Service/mar-bin.00000%d'", Test->maxscale_sshkey, Test->maxscale_IP, Test->maxdir, i);
        ls = popen(sys, "r");
        fgets(buf_max, sizeof(buf), ls);
        pclose(ls);
        x = strchr(buf_max, ' '); x[0] = 0;
        printf("Binlog checksum from Maxscale %s\n", buf_max);


        sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s 'sha1sum /var/lib/mysql/mar-bin.00000%d'", Test->repl->sshkey[0], Test->repl->IP[0], i);
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
    return(global_result);
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
    return(global_result);
}

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;

    int i;

    Test->read_env();
    Test->print_env();

    for (int option = 0; option < 3; option++) {
        Test->binlog_cmd_option = option;
        Test->start_binlog();

        Test->repl->connect();

        create_t1(Test->repl->nodes[0]);
        global_result += insert_into_t1(Test->repl->nodes[0], 4);
        printf("Sleeping to let replication happen\n"); fflush(stdout);
        sleep(30);

        for (i = 0; i < Test->repl->N; i++) {
            printf("Checking data from node %d (%s)\n", i, Test->repl->IP[i]); fflush(stdout);
            global_result += select_from_t1(Test->repl->nodes[i], 4);
        }

        printf("First transaction test (with ROLLBACK)\n");
        start_transaction(Test);

        printf("SELECT * FROM t1 WHERE fl=10, checking inserted values\n");
        global_result += execute_query_check_one(Test->repl->nodes[0], (char *) "SELECT * FROM t1 WHERE fl=10", "111");

        //printf("SELECT, checking inserted values from slave\n");
        //global_result += execute_query_check_one(Test->repl->nodes[2], (char *) "SELECT * FROM t1 WHERE fl=10", "111");

        global_result += check_sha1(Test);

        printf("ROLLBACK\n");
        global_result += execute_query(Test->repl->nodes[0], (char *) "ROLLBACK");
        printf("INSERT INTO t1 VALUES(112, 10)\n");
        global_result += execute_query(Test->repl->nodes[0], (char *) "INSERT INTO t1 VALUES(112, 10)");
        sleep(20);

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

        global_result += check_sha1(Test);
        Test->repl->close_connections();
    }


    Test->copy_all_logs(); return(global_result);
}


