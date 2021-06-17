/**
 * MXS-1476: priority value ignored when a Galera node rejoins with a lower wsrep_local_index than current
 * master
 *
 * https://jira.mariadb.org/browse/MXS-1476
 */

#include <maxtest/testconnections.hh>
#include <maxtest/galera_cluster.hh>

void list_servers(TestConnections& test)
{
    auto output = test.maxscale->ssh_output("maxctrl list servers");
    test.tprintf("%s", output.output.c_str());
}

void do_test(TestConnections& test, int master, int slave)
{
    test.maxscale->connect_maxscale();
    test.try_query(test.maxscale->conn_rwsplit[0], "DROP TABLE IF EXISTS test.t1");
    test.try_query(test.maxscale->conn_rwsplit[0], "CREATE TABLE test.t1 (id int)");
    test.try_query(test.maxscale->conn_rwsplit[0], "INSERT INTO test.t1 VALUES (1)");

    test.tprintf("Stop a slave node and perform an insert");
    test.galera->block_node(slave);
    test.maxscale->wait_for_monitor();
    list_servers(test);

    test.try_query(test.maxscale->conn_rwsplit[0], "INSERT INTO test.t1 VALUES (1)");

    test.tprintf("Start the slave node and perform another insert");
    test.galera->unblock_node(slave);
    test.maxscale->wait_for_monitor();
    list_servers(test);

    test.try_query(test.maxscale->conn_rwsplit[0], "INSERT INTO test.t1 VALUES (1)");
    test.maxscale->close_maxscale_connections();

    test.tprintf("Stop the master node and perform an insert");
    test.galera->block_node(master);
    test.maxscale->wait_for_monitor();
    list_servers(test);

    test.maxscale->connect_maxscale();
    test.try_query(test.maxscale->conn_rwsplit[0], "INSERT INTO test.t1 VALUES (1)");

    test.tprintf("Start the master node and perform another insert (expecting failure)");
    test.galera->unblock_node(master);
    test.maxscale->wait_for_monitor();
    list_servers(test);

    test.add_result(execute_query_silent(test.maxscale->conn_rwsplit[0],
                                         "INSERT INTO test.t1 VALUES (1)") == 0,
                    "Query should fail");
    test.maxscale->close_maxscale_connections();

    test.maxscale->connect_maxscale();
    test.try_query(test.maxscale->conn_rwsplit[0], "DROP TABLE test.t1");
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.galera->stop_node(2);
    test.galera->stop_node(3);

    do_test(test, 1, 0);

    test.tprintf("Swap the priorities around and run the test again");
    test.maxscale->ssh_node_f(true,
                              "sed -i 's/priority=1/priority=3/' /etc/maxscale.cnf;"
                              "sed -i 's/priority=2/priority=1/' /etc/maxscale.cnf;"
                              "sed -i 's/priority=3/priority=2/' /etc/maxscale.cnf;");
    test.maxscale->restart_maxscale();

    // Give the Galera nodes some time to stabilize
    sleep(5);

    do_test(test, 0, 1);

    test.galera->start_node(2);
    test.galera->start_node(3);
    return test.global_result;
}
