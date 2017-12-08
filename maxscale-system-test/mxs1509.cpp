/**
 * MXS-1509: Show correct server state for multisource replication
 *
 * https://jira.mariadb.org/browse/MXS-1509
 */

#include "testconnections.h"
#include <sstream>
#include "maxscales.h"

void change_master(TestConnections& test, int slave, int master, const char* name = NULL)
{
    std::string source;

    if (name)
    {
        source = "'";
        source += name;
        source += "'";
    }

    execute_query(test.repl->nodes[slave],
                  "STOP ALL SLAVES;CHANGE MASTER %s TO master_host='%s', master_port=3306, "
                  "master_user='%s', master_password='%s', master_use_gtid=slave_pos;START ALL SLAVES",
                  source.c_str(), test.repl->IP[master], test.repl->user_name, test.repl->password, source.c_str());
}

const char* dump_status(const StringSet& current, const StringSet& expected)
{
    std::stringstream ss;
    ss << "Current status: (";

    for (const auto& a : current)
    {
        ss << a << ",";
    }

    ss << ") Expected status: (";

    for (const auto& a : expected)
    {
        ss << a << ",";
    }

    ss << ")";

    static std::string res = ss.str();
    return res.c_str();
}

void check_status(TestConnections& test, const StringSet& expected_master, const StringSet& expected_slave)
{
    sleep(2);
    StringSet master = test.maxscales->get_server_status(0, "server1");
    StringSet slave = test.maxscales->get_server_status(0, "server2");
    test.add_result(master != expected_master, "Master status is not what was expected: %s",
                    dump_status(master, expected_master));
    test.add_result(slave != expected_slave, "Slave status is not what was expected: %s",
                    dump_status(slave, expected_slave));
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    // Stop replication on nodes three and four
    test.repl->connect();
    execute_query(test.repl->nodes[2], "STOP ALL SLAVES; RESET SLAVE ALL;");
    execute_query(test.repl->nodes[3], "STOP ALL SLAVES; RESET SLAVE ALL;");

    // Point the master to an external server
    change_master(test, 1, 0);
    change_master(test, 0, 2);
    check_status(test, {"Master", "Running"}, {"Slave", "Running"});

    // Resetting the slave on master should have no effect
    execute_query(test.repl->nodes[0], "STOP ALL SLAVES; RESET SLAVE ALL;");
    check_status(test, {"Master", "Running"}, {"Slave", "Running"});

    // Configure multi-source replication, check that master status is as expected
    change_master(test, 0, 2, "extra-slave");
    change_master(test, 1, 2, "extra-slave");
    check_status(test, {"Master", "Running"}, {"Slave", "Running", "Slave of External Server"});

    // Stopping multi-source replication on slave should remove the Slave of External Server status
    execute_query(test.repl->nodes[1], "STOP SLAVE 'extra-slave'; RESET SLAVE 'extra-slave';");
    check_status(test, {"Master", "Running"}, {"Slave", "Running"});

    // Doing the same on the master should have no effect
    execute_query(test.repl->nodes[0], "STOP ALL SLAVES; RESET SLAVE ALL;");
    check_status(test, {"Master", "Running"}, {"Slave", "Running"});

    // Cleanup
    test.repl->execute_query_all_nodes( "STOP ALL SLAVES; RESET SLAVE ALL;");
    test.repl->fix_replication();
    return test.global_result;
}
