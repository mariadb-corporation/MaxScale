/**
 * @file bug519.cpp
 * - fill t1 wuth data
 * - execute SELECT * INTO OUTFILE '/tmp/t1.csv' FROM t1; against all routers
 * - DROP TABLE t1
 * - LOAD DATA LOCAL INFILE 't1.csv' INTO TABLE t1; using RWSplit
 * - check if t1 contains right data
 * - DROP t1 again and repeat LOAD DATA LOCAL INFILE 't1.csv' INTO TABLE t1; using ReadConn master
 */


#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int N=4;
    int iterations = 2;
    if (Test->smoke) {iterations = 1;}
    char str[1024];
    Test->set_timeout(10);

    Test->connect_maxscale();
    Test->repl->connect();

    Test->tprintf("Create t1\n");
    create_t1(Test->conn_rwsplit);
    Test->tprintf("Insert data into t1\n");
    Test->set_timeout(60);
    insert_into_t1(Test->conn_rwsplit, N);
    Test->tprintf("Sleeping to let replication happen\n");
    Test->stop_timeout();
    sleep(30);
    Test->set_timeout(200);

    sprintf(str, "%s rm /tmp/t*.csv; %s chmod 777 /tmp", Test->repl->access_sudo[0], Test->repl->access_sudo[0]);
    Test->tprintf("%s\n", str);
    Test->repl->ssh_node(0, str, false);
    system(str);

    Test->tprintf("Copying data from t1 to file...\n");
    Test->tprintf("using RWSplit: SELECT * INTO OUTFILE '/tmp/t1.csv' FROM t1;\n");
    Test->try_query(Test->conn_rwsplit, (char *) "SELECT * INTO OUTFILE '/tmp/t1.csv' FROM t1;");
    Test->tprintf("using ReadConn master: SELECT * INTO OUTFILE '/tmp/t2.csv' FROM t1;\n");
    Test->try_query(Test->conn_master, (char *) "SELECT * INTO OUTFILE '/tmp/t2.csv' FROM t1;");
    Test->tprintf("using ReadConn slave: SELECT * INTO OUTFILE '/tmp/t3.csv' FROM t1;\n");
    Test->try_query(Test->conn_slave, (char *) "SELECT * INTO OUTFILE '/tmp/t3.csv' FROM t1;");

    Test->tprintf("Copying t1.cvs from Maxscale machine:\n");
    sprintf(str, "scp -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no  -o LogLevel=quiet %s@%s:/tmp/t1.csv ./", Test->repl->sshkey[0], Test->repl->access_user[0], Test->repl->IP[0]);
    Test->tprintf("%s\n", str);
    system(str);

    MYSQL *srv[2];

    srv[0] = Test->conn_rwsplit;
    srv[1] = Test->conn_master;
    for (int i = 0; i < iterations; i++) {
        Test->set_timeout(100);
        Test->tprintf("Dropping t1 \n");
        Test->try_query(Test->conn_rwsplit, (char *) "DROP TABLE t1;");
        Test->tprintf("Sleeping to let replication happen\n");
        Test->stop_timeout();
        sleep(50);
        Test->set_timeout(100);
        Test->tprintf("Create t1\n");
        create_t1(Test->conn_rwsplit);
        Test->tprintf("Loading data to t1 from file\n");
        Test->try_query(srv[i], (char *) "LOAD DATA LOCAL INFILE 't1.csv' INTO TABLE t1;");

        Test->tprintf("Sleeping to let replication happen\n");
        Test->stop_timeout();
        sleep(50);
        Test->set_timeout(100);
        Test->tprintf("SELECT: rwsplitter\n");
        Test->add_result(select_from_t1(Test->conn_rwsplit, N), "Wrong data in 't1'");
        Test->tprintf("SELECT: master\n");
        Test->add_result(select_from_t1(Test->conn_master, N), "Wrong data in 't1'");
        Test->tprintf("SELECT: slave\n");
        Test->add_result(select_from_t1(Test->conn_slave, N), "Wrong data in 't1'");
        Test->tprintf("Sleeping to let replication happen\n");
    }

    Test->repl->close_connections();
    Test->check_maxscale_alive();

    Test->copy_all_logs(); return(Test->global_result);
}

