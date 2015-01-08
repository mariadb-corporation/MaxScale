/**
 * @file bug676.cpp  reproducing attempt ("Memory corruption when users with long hostnames that can no the resolved are loaded into MaxScale")
 *
 *
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
    int i;
    char sys1[4096];

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

    printf("Stopping %d\n", 0); fflush(stdout);
    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s '/etc/init.d/mysql stop'", Test->galera->sshkey[0], Test->galera->IP[0]);
    printf("%s\n", sys1);  fflush(stdout);
    system(sys1); fflush(stdout);

    printf("selecting DB 'test' for rwsplit\n"); fflush(stdout);
    global_result += execute_query(conn, "USE test");

    printf("Closing connection\n"); fflush(stdout);
    mysql_close(conn);

    Test->ConnectRWSplit();
    global_result += execute_query(Test->conn_rwsplit, "show processlist;");
    Test->CloseMaxscaleConn();

    printf("Stopping all Galera nodes\n");  fflush(stdout);
    for (i = 1; i < Test->galera->N; i++) {
        printf("Stopping %d\n", i); fflush(stdout);
        sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s '/etc/init.d/mysql stop'", Test->galera->sshkey[i], Test->galera->IP[i]);
        printf("%s\n", sys1);  fflush(stdout);
        system(sys1); fflush(stdout);

        sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s 'sed -i \"s/wsrep_sst_method=rsync/wsrep_sst_method=xtrabackup-v2/\" /etc/my.cnf.d/skysql-galera.cnf'", Test->galera->sshkey[i], Test->galera->IP[i]);
        printf("%s\n", sys1);  fflush(stdout);
        system(sys1); fflush(stdout);
    }

    printf("Restarting Galera cluster\n");
    printf("Starting back all Galera nodes\n");  fflush(stdout);
    printf("Starting node %d\n", Test->galera->N-2); fflush(stdout);
    sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s '/etc/init.d/mysql start --wsrep-cluster-address=gcomm://'", Test->galera->sshkey[0], Test->galera->IP[0]);
    printf("%s\n", sys1);  fflush(stdout);
    system(sys1); fflush(stdout);

    for (i = 1; i < Test->galera->N; i++) {
        printf("Starting node %d\n", i); fflush(stdout);
        sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s '/etc/init.d/mysql start --wsrep-cluster-address=gcomm://%s'", Test->galera->sshkey[i], Test->galera->IP[i], Test->galera->IP[0]);
        printf("%s\n", sys1);  fflush(stdout);
        system(sys1); fflush(stdout);
    }

    return(global_result);
}

