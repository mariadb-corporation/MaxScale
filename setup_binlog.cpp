/**
 * @file setup_binlog test of simple binlog router setup
 * setup one master, one slave directly connected to real master and two slaves connected to binlog router
 * create table and put data into it using connection to master
 * check data using direct commection to all backend
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;
    char sys[1024];
    int i;
    char * x;

    FILE *ls;

    char buf[1024];

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

        Test->repl->close_connections();

        for (i = 0; i < Test->repl->N; i++) {
            sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s 'sha1sum /var/lib/mysql/mar-bin.000001'", Test->repl->sshkey[i], Test->repl->IP[i]);
            //system(sys);
            ls = popen(sys, "r");
            fgets(buf, sizeof(buf), ls);
            pclose(ls);
            printf("%s\n", buf);
            x = strchr(buf, ' '); x[0] = 0;
            printf("%s\n", buf);
        }
    }
    sprintf(sys, "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s 'sha1sum %s/Binlog_Service/mar-bin.000001'", Test->maxscale_sshkey, Test->maxscale_IP, Test->maxdir);
    //system(sys);
    ls = popen(sys, "r");
    fgets(buf, sizeof(buf), ls);
    pclose(ls);
    printf("%s\n", buf);
    x = strchr(buf, ' '); x[0] = 0;
    printf("%s\n", buf);

    Test->copy_all_logs(); return(global_result);
}


