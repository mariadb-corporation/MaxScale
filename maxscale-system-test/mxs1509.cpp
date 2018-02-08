/**
 * MXS-1509: Show correct server state for multisource replication
 *
 * https://jira.mariadb.org/browse/MXS-1509
 */

#include "testconnections.h"
#include <sstream>

void change_master(TestConnections& test, int slave, int master, const char* name = NULL)
{
    std::string source;

    if (name)
    {
        source = "'";
        source += name;
        source += "'";
    }

    execute_query(test.repl->nodes[slave], "STOP ALL SLAVES;"
                  "SET GLOBAL gtid_slave_pos='0-1-0';"
                  "CHANGE MASTER %s TO master_host='%s', master_port=3306, master_user='%s', master_password='%s', master_use_gtid=slave_pos;"
                  "START ALL SLAVES",
                  source.c_str(), test.repl->IP[master], test.repl->user_name, test.repl->password, source.c_str());
}

void check_status(TestConnections& test, const StringSet& expected_master, const StringSet& expected_slave)
{
    sleep(2);
    StringSet master = test.get_server_status("server1");
    StringSet slave = test.get_server_status("server2");
    test.add_result(master != expected_master, "Master status is not what was expected: %s",
                    dump_status(master, expected_master).c_str());
    test.add_result(slave != expected_slave, "Slave status is not what was expected: %s",
                    dump_status(slave, expected_slave).c_str());
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.repl->connect();
    test.tprintf("Server sanity check");
    check_status(test, {"Master", "Running"}, {"Slave", "Running"});

    test.tprintf("Stop replication on nodes three and four");
    execute_query(test.repl->nodes[2], "STOP ALL SLAVES; RESET SLAVE ALL;");
    execute_query(test.repl->nodes[3], "STOP ALL SLAVES; RESET SLAVE ALL;");

    test.tprintf("Point the master to an external server");
    change_master(test, 1, 0);
    change_master(test, 0, 2);
    check_status(test, {"Master", "Running"}, {"Slave", "Running"});

    test.tprintf("Resetting the slave on master should have no effect");
    execute_query(test.repl->nodes[0], "STOP ALL SLAVES; RESET SLAVE ALL;");
    check_status(test, {"Master", "Running"}, {"Slave", "Running"});

    // TODO: Fix this so that multi-source replication is tested
    // test.tprintf("Configure multi-source replication, check that master status is as expected");
    // change_master(test, 0, 2, "extra-slave");
    // change_master(test, 1, 2, "extra-slave");
    // check_status(test, {"Master", "Running"}, {"Slave", "Running", "Slave of External Server"});

    // test.tprintf("Stopping multi-source replication on slave should remove the Slave of External Server status");
    // execute_query(test.repl->nodes[1], "STOP SLAVE 'extra-slave'; RESET SLAVE 'extra-slave';");
    // check_status(test, {"Master", "Running"}, {"Slave", "Running"});
    // sleep(60);

    // test.tprintf("Doing the same on the master should have no effect");
    // execute_query(test.repl->nodes[0], "STOP ALL SLAVES; RESET SLAVE ALL;");
    // check_status(test, {"Master", "Running"}, {"Slave", "Running"});

    test.tprintf("Cleanup");
    test.repl->execute_query_all_nodes( "STOP ALL SLAVES; RESET SLAVE ALL;");
    test.repl->fix_replication();
    return test.global_result;
}
