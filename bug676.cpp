/**
 * @file bug676.cpp  reproducing attempt for bug676 ("Memory corruption when users with long hostnames that can no the resolved are loaded into MaxScale")
 * - Configuration
 * @verbatim
[MySQL Monitor]
type=monitor
module=galeramon
servers=server1,server2,server3
user=skysql
passwd=skysql

[RW Split Router]
type=service
router=readwritesplit
servers=server1,server2,server3
#user=maxpriv
#passwd=maxpwd
user=skysql
passwd=skysql
filters=MyLogFilter
version_string=MariaDBEC-10.0.14
localhost_match_wildcard_host=1
max_slave_connections=1

[Read Connection Router]
type=service
router=readconnroute
router_options=synced
servers=server1,server2,server3
user=skysql
passwd=skysql

[Debug Interface]
type=service
router=debugcli

[RW Split Listener]
type=listener
service=RW Split Router
protocol=MySQLClient
port=4006

[Read Connection Listener]
type=listener
service=Read Connection Router
protocol=MySQLClient
port=4008

[Debug Listener]
type=listener
service=Debug Interface
protocol=telnetd
port=4442

[CLI]
type=service
router=cli

[CLI Listener]
type=listener
service=CLI
protocol=maxscaled
#address=localhost
port=6603

[MyLogFilter]
type=filter
module=qlafilter
options=/tmp/QueryLog
 @endverbatim
 * - connect to RWSplit
 * - stop node0
 * - sleep 60 seconds
 * - reconnect
 * - check if 'USE test ' is ok
 * - check MaxScale is alive
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "mariadb_func.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argv[0]);
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

    sleep(60);
    mysql_close(conn);

    conn = open_conn_no_db(Test->rwsplit_port, Test->Maxscale_IP, Test->Maxscale_User, Test->Maxscale_Password);

    if (conn == 0) {
        printf("Error connection to RW Split\n");
        exit(1);
    }

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

        //sprintf(&sys1[0], "ssh -i %s -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@%s 'sed -i \"s/wsrep_sst_method=rsync/wsrep_sst_method=xtrabackup-v2/\" /etc/my.cnf.d/skysql-galera.cnf'", Test->galera->sshkey[i], Test->galera->IP[i]);
        //printf("%s\n", sys1);  fflush(stdout);
        //system(sys1); fflush(stdout);
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

