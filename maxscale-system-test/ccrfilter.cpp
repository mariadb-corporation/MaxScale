/**
 * @file ccrfilter.cpp Tests for the CCRFilter module
 * - configure Maxscale to use Consistent Critical Read Filter
 * - configure CCR filter with parameter 'time=10'
 * - Execute INSERT
 * - check that SELECT goes to Master
 * - wait 11 seconds
 * - chat that SELECT goes to slave
 * - configure CCR filter with parameter 'count=3'
 * - execute INSERT
 * - execute 5 SELECTs, check that first 3 go to Master, 2 last - to slave
 * - configure CCR filter with parameter 'match=t2'
 * - execute INSERT INTO  t1
 * - check SELECTs go to SLAVE
 * - execute INSERT INTO  t2
 * - check SELECTs go to Master
 * - configure CCR filter with parameter 'ignore=t1' and remove parameter 'match=t2'
 * - execute INSERT INTO  t1
 * - check SELECTs go to SLAVE
 * - execute INSERT INTO  t2
 * - check SELECTs go to Master
 */

#include <iostream>
#include <unistd.h>
#include "testconnections.h"

static int master_id;

bool is_master(MYSQL *conn)
{
    char str[1024];

    if (find_field(conn, "SELECT @@server_id", "@@server_id", str) == 0)
    {
        int server_id = atoi(str);
        return server_id == master_id;
    }

    return false;
}

int main(int argc, char *argv[])
{
    TestConnections * test = new TestConnections(argc, argv);

    test->repl->connect();

    /**
     * Get the master's @@server_id
     */
    master_id = test->repl->get_server_id(0);
    test->tprintf("Master server_id: %d", master_id);


    execute_query(test->repl->nodes[0], "CREATE OR REPLACE TABLE test.t1 (id INT);");
    execute_query(test->repl->nodes[0], "CREATE OR REPLACE TABLE test.t2 (id INT);");

    test->maxscales->connect_maxscale(0);

    test->tprintf("Test `time`. The first SELECT within 10 seconds should go the "
                  "master and all SELECTs after it should go to the slaves.");

    test->try_query(test->maxscales->conn_rwsplit[0], "INSERT INTO test.t1 VALUES (1)");
    sleep(1);
    test->add_result(!is_master(test->maxscales->conn_rwsplit[0]), "Master should reply to the first SELECT");
    sleep(11);
    test->add_result(is_master(test->maxscales->conn_rwsplit[0]), "Master should NOT reply to the second SELECT");


    test->tprintf("Change test setup for `count`, the first three selects after an "
                  "insert should go to the master.");

    test->maxscales->close_maxscale_connections(0);
    test->maxscales->ssh_node(0, "sed -i -e 's/time.*/time=0/' /etc/maxscale.cnf", true);
    test->maxscales->ssh_node(0, "sed -i -e 's/###count/count/' /etc/maxscale.cnf", true);
    test->maxscales->restart_maxscale(0);
    test->maxscales->connect_maxscale(0);

    test->try_query(test->maxscales->conn_rwsplit[0], "INSERT INTO test.t1 VALUES (1)");
    test->add_result(!is_master(test->maxscales->conn_rwsplit[0]), "Master should reply to the first SELECT");
    test->add_result(!is_master(test->maxscales->conn_rwsplit[0]), "Master should reply to the second SELECT");
    test->add_result(!is_master(test->maxscales->conn_rwsplit[0]), "Master should reply to the third SELECT");
    test->add_result(is_master(test->maxscales->conn_rwsplit[0]), "Master should NOT reply to the fourth SELECT");
    test->add_result(is_master(test->maxscales->conn_rwsplit[0]), "Master should NOT reply to the fifth SELECT");


    test->tprintf("Change test setup for `count` and `match`, selects after an insert "
                  "to t1 should go to the slaves and selects after an insert to t2 "
                  "should go to the master.");

    test->maxscales->close_maxscale_connections(0);
    test->maxscales->ssh_node(0, "sed -i -e 's/###match/match/' /etc/maxscale.cnf", true);
    test->maxscales->restart_maxscale(0);
    test->maxscales->connect_maxscale(0);


    test->tprintf("t1 first, should be ignored");

    test->try_query(test->maxscales->conn_rwsplit[0], "INSERT INTO test.t1 VALUES (1)");
    test->add_result(is_master(test->maxscales->conn_rwsplit[0]), "Master should NOT reply to the first SELECT");
    test->add_result(is_master(test->maxscales->conn_rwsplit[0]), "Master should NOT reply to the second SELECT");

    test->tprintf("t2 should match and trigger the critical reads");

    test->try_query(test->maxscales->conn_rwsplit[0], "INSERT INTO test.t2 VALUES (1)");
    test->add_result(!is_master(test->maxscales->conn_rwsplit[0]), "Master should reply to the first SELECT");
    test->add_result(!is_master(test->maxscales->conn_rwsplit[0]), "Master should reply to the second SELECT");
    test->add_result(!is_master(test->maxscales->conn_rwsplit[0]), "Master should reply to the third SELECT");
    test->add_result(is_master(test->maxscales->conn_rwsplit[0]), "Master should NOT reply to the fourth SELECT");
    test->add_result(is_master(test->maxscales->conn_rwsplit[0]), "Master should NOT reply to the fifth SELECT");


    test->tprintf("Change test setup for `count` and `ignore`, expects the same "
                  "results as previous test.");

    test->maxscales->close_maxscale_connections(0);
    test->maxscales->ssh_node(0, "sed -i -e 's/match/###match/' /etc/maxscale.cnf", true);
    test->maxscales->ssh_node(0, "sed -i -e 's/###ignore/ignore/' /etc/maxscale.cnf", true);
    test->maxscales->restart_maxscale(0);
    test->maxscales->connect_maxscale(0);

    test->tprintf("t1 first, should be ignored");

    test->try_query(test->maxscales->conn_rwsplit[0], "INSERT INTO test.t1 VALUES (1)");
    test->add_result(is_master(test->maxscales->conn_rwsplit[0]), "Master should NOT reply to the first SELECT");
    test->add_result(is_master(test->maxscales->conn_rwsplit[0]), "Master should NOT reply to the second SELECT");

    test->tprintf("t2 should match and trigger the critical reads");

    test->try_query(test->maxscales->conn_rwsplit[0], "INSERT INTO test.t2 VALUES (1)");
    test->add_result(!is_master(test->maxscales->conn_rwsplit[0]), "Master should reply to the first SELECT");
    test->add_result(!is_master(test->maxscales->conn_rwsplit[0]), "Master should reply to the second SELECT");
    test->add_result(!is_master(test->maxscales->conn_rwsplit[0]), "Master should reply to the third SELECT");
    test->add_result(is_master(test->maxscales->conn_rwsplit[0]), "Master should NOT reply to the fourth SELECT");
    test->add_result(is_master(test->maxscales->conn_rwsplit[0]), "Master should NOT reply to the fifth SELECT");

    execute_query(test->repl->nodes[0], "DROP TABLE test.t1");
    execute_query(test->repl->nodes[0], "DROP TABLE test.t2");

    int rval = test->global_result;
    delete test;
    return rval;
}
