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
 * - sleep 30 seconds
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
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(20);

    int i;
    char sys1[4096];

    MYSQL * conn = open_conn_no_db(Test->rwsplit_port, Test->maxscale_IP, Test->maxscale_user, Test->maxscale_password, Test->ssl);

    Test->tprintf("Stopping %d\n", 0);
    Test->galera->stop_node(0);

    Test->stop_timeout();
    sleep(30);
    Test->set_timeout(20);
    mysql_close(conn);

    conn = open_conn_no_db(Test->rwsplit_port, Test->maxscale_IP, Test->maxscale_user, Test->maxscale_password, Test->ssl);

    if (conn == 0) {
        Test->add_result(1, "Error connection to RW Split\n");
        Test->copy_all_logs();
        exit(1);
    }

    Test->tprintf("selecting DB 'test' for rwsplit\n");
    Test->try_query(conn, "USE test");

    Test->tprintf("Closing connection\n");
    mysql_close(conn);

    Test->connect_rwsplit();
    Test->try_query(Test->conn_rwsplit, "show processlist;");
    Test->close_maxscale_connections();

    Test->tprintf("Stopping all Galera nodes\n");
    for (i = 1; i < Test->galera->N; i++) {
        Test->set_timeout(30);
        Test->tprintf("Stopping %d\n", i);
        Test->galera->stop_node(i);
         fflush(stdout);
    }

    Test->tprintf("Restarting Galera cluster\n");
    Test->galera->start_galera();

    Test->copy_all_logs(); return(Test->global_result);
}

