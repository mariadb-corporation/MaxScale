/**
 * @file MXS-1737_galeramon_test.cpp Regression case for the bug "Existing ReadWriteSplit connection can't find master after the master changes"
 * - open connection to RW Split router
 * - execute query
 * - block "master"
 * - sleep
 * - execute query
 * - check maxadmin output to find a new master
 * - unblock master
 * - sleep
 * - execute query
 * - check maxadmin output to find curemt master
 */

#include "testconnections.h"

#define SLEEP_TIME 10

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections (argc, argv);
    Test->connect_maxscale();

    Test->try_query(Test->conn_rwsplit, "SELECT 1");
    int master1 = Test->find_master_maxadmin(Test->galera);
    Test->tprintf("Master is %03d\nStopping master\n", master1);

    Test->galera->block_node(master1);
    sleep(SLEEP_TIME);

    Test->try_query(Test->conn_rwsplit, "SELECT 1");

    int master2 = Test->find_master_maxadmin(Test->galera);
    Test->tprintf("Master is %03d\n", master2);

    Test->galera->unblock_node(master1);
    sleep(SLEEP_TIME);

    Test->try_query(Test->conn_rwsplit, "SELECT 1");

    int master3 = Test->find_master_maxadmin(Test->galera);
    Test->tprintf("Master is %03d\nStopping master\n", master3);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
