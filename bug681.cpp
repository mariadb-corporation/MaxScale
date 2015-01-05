/**
 * @file bug681.cpp  - regression test for bug681 ("crash if max_slave_connections=10% and 4 or less backends are configured")
 *
 * - Configure RWSplit with max_slave_connections=10%
 * - check MaxScale is alive
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "mariadb_func.h"

int main()
{
    TestConnections * Test = new TestConnections();
    int global_result = 0;
    //int i;
    //char sys1[4096];

    Test->ReadEnv();
    Test->PrintIP();

    /*
    printf("Stopping MaxScale");
    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s 'service maxscale stop'", Test->Maxscale_sshkey, Test->Maxscale_IP);
    printf("%s\n", sys1);  fflush(stdout);
    system(sys1); fflush(stdout);


    printf("Stopping all Galera nodes\n");  fflush(stdout);
    for (i = 0; i < Test->galera->N; i++) {
        printf("Stopping %d\n", i); fflush(stdout);
        sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s '/etc/init.d/mysql stop'", Test->galera->sshkey[i], Test->galera->IP[i]);
        printf("%s\n", sys1);  fflush(stdout);
        system(sys1); fflush(stdout);

        sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s 'sed -i \"s/wsrep_sst_method=rsync/wsrep_sst_method=xtrabackup-v2/\" /etc/my.cnf.d/skysql-galera.cnf'", Test->galera->sshkey[i], Test->galera->IP[i]);
        printf("%s\n", sys1);  fflush(stdout);
        system(sys1); fflush(stdout);
    }

    printf("Starting back all Galera nodes\n");  fflush(stdout);
    printf("Starting node %d\n", Test->galera->N-2); fflush(stdout);
    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s '/etc/init.d/mysql start --wsrep-cluster-address=gcomm://'", Test->galera->sshkey[Test->galera->N-2], Test->galera->IP[Test->galera->N-2]);
    printf("%s\n", sys1);  fflush(stdout);
    system(sys1); fflush(stdout);

    for (i = 0; i < Test->galera->N; i++) {
        if ( i != Test->galera->N-2 ) {
            printf("Starting node %d\n", i); fflush(stdout);
            sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s '/etc/init.d/mysql start --wsrep-cluster-address=gcomm://%s'", Test->galera->sshkey[i], Test->galera->IP[i], Test->galera->IP[Test->galera->N-2]);
            printf("%s\n", sys1);  fflush(stdout);
            system(sys1); fflush(stdout);
        }
    }

    printf("Starting MaxScale");
    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s 'service maxscale start'", Test->Maxscale_sshkey, Test->Maxscale_IP);
    printf("%s\n", sys1);  fflush(stdout);
    system(sys1); fflush(stdout);

    sleep(10);
    */


    MYSQL * conn = open_conn_no_db(Test->rwsplit_port, Test->Maxscale_IP, Test->Maxscale_User, Test->Maxscale_Password);

    if (conn != NULL) {
        global_result++;
        printf("FAILS: RWSplit services should fail, but it is started\n"); fflush(stdout);
    }


    Test->ConnectReadMaster();
    Test->CloseReadSlave();

    printf("Trying query to ReadConn master\n"); fflush(stdout);
    global_result += execute_query(Test->conn_master, "show processlist;");
    printf("Trying query to ReadConn slave\n"); fflush(stdout);
    global_result += execute_query(Test->conn_slave, "show processlist;");

    Test->CloseMaxscaleConn();

    global_result    += CheckLogErr((char *) "Error : Unable to start RW Split Router service. There are too few backend servers configured in MaxScale.cnf. Found 10% when at least 33% would be required", TRUE);

    return(global_result);
}


