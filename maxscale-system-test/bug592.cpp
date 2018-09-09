/**
 * @file bug592.cpp  regression case for bug 592 ( "slave in "Running" state breaks authorization" ) MXS-326
 *
 * - stop all slaves: "stop slave;" directly to every node (now they are in "Running" state, not in "Russning,
 *Slave")
 * - via RWSplit "CREATE USER 'test_user'@'%' IDENTIFIED BY 'pass'"
 * - try to connect using 'test_user' (expecting success)
 * - start all slaves: "start slave;" directly to every node
 * - via RWSplit: "DROP USER 'test_user'@'%'"
 */

/*
 *  Timofey Turenko 2014-10-24 09:35:35 UTC
 *  1. setup: Master/Slave replication
 *  2. reboot slaves
 *  3. create user usinf connection to RWSplit
 *  4. try to use this user to connect to Maxscale
 *
 *  expected result:
 *  Authentication is ok
 *
 *  actual result:
 *  Access denied for user 'user'@'192.168.122.1' (using password: YES)
 *
 *  Th issue was discovered with following setup state:
 *
 *  MaxScale> show servers
 *  Server 0x3428260 (server1)
 *   Server:             192.168.122.106
 *   Status:                     Master, Running
 *   Protocol:           MySQLBackend
 *   Port:               3306
 *   Server Version:         5.5.40-MariaDB-log
 *   Node Id:            106
 *   Master Id:          -1
 *   Slave Ids:          107, 108 , 109
 *   Repl Depth:         0
 *   Number of connections:      0
 *   Current no. of conns:       0
 *   Current no. of operations:  0
 *  Server 0x3428160 (server2)
 *   Server:             192.168.122.107
 *   Status:                     Slave, Running
 *   Protocol:           MySQLBackend
 *   Port:               3306
 *   Server Version:         5.5.40-MariaDB-log
 *   Node Id:            107
 *   Master Id:          106
 *   Slave Ids:
 *   Repl Depth:         1
 *   Number of connections:      0
 *   Current no. of conns:       0
 *   Current no. of operations:  0
 *  Server 0x3428060 (server3)
 *   Server:             192.168.122.108
 *   Status:                     Running
 *   Protocol:           MySQLBackend
 *   Port:               3306
 *   Server Version:         5.5.40-MariaDB-log
 *   Node Id:            108
 *   Master Id:          106
 *   Slave Ids:
 *   Repl Depth:         1
 *   Number of connections:      0
 *   Current no. of conns:       0
 *   Current no. of operations:  0
 *  Server 0x338c3f0 (server4)
 *   Server:             192.168.122.109
 *   Status:                     Running
 *   Protocol:           MySQLBackend
 *   Port:               3306
 *   Server Version:         5.5.40-MariaDB-log
 *   Node Id:            109
 *   Master Id:          106
 *   Slave Ids:
 *   Repl Depth:         1
 *   Number of connections:      0
 *   Current no. of conns:       0
 *   Current no. of operations:  0
 *
 *
 *  Maxscale read mysql.user table from server4 which was not properly replicated
 *  Comment 1 Mark Riddoch 2014-11-05 09:55:07 UTC
 *  In the reload users routine, if there is a master available then use that rather than the first.
 */



#include <iostream>
#include "testconnections.h"

int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);
    Test->set_timeout(10);
    int i;

    Test->repl->connect();
    Test->maxscales->connect_maxscale(0);

    for (i = 1; i < Test->repl->N; i++)
    {
        execute_query(Test->repl->nodes[i], (char*) "stop slave;");
    }

    execute_query(Test->maxscales->conn_rwsplit[0],
                  (char*) "CREATE USER 'test_user'@'%%' IDENTIFIED BY 'pass'");

    MYSQL* conn = open_conn_no_db(Test->maxscales->rwsplit_port[0],
                                  Test->maxscales->IP[0],
                                  (char*) "test_user",
                                  (char*) "pass",
                                  Test->ssl);

    if (conn == NULL)
    {
        Test->add_result(1, "Connections error\n");
    }

    for (i = 1; i < Test->repl->N; i++)
    {
        execute_query(Test->repl->nodes[i], (char*) "start slave;");
    }

    execute_query(Test->maxscales->conn_rwsplit[0], (char*) "DROP USER 'test_user'@'%%'");

    Test->repl->close_connections();
    Test->maxscales->close_maxscale_connections(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
