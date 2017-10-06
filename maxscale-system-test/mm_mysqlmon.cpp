/**
 * @file mm_mysqlmon.cpp MySQL Monitor Multi-master Test
 * - Configure all servers into a multi-master ring with one slave
 * - check status using Maxadmin 'show servers' and 'show monitor "MySQL Monitor"'
 * - Set nodes 0 and 1 into read-only mode
 * - repeat status check
 * - Configure nodes 1 and 2 (server2 and server3) into a master-master pair, make node 0 a slave of node 1 and node 3 a slave of node 2
 * - repeat status check
 * - Set node 1 into read-only mode
 * - repeat status check
 * - Create two distinct groups (server1 and server2 are masters for eache others and same for server3 and server4)
 * - repeat status check
 * - Set nodes 1 and 3 (server2 and server4) into read-only mode
 */


#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

void check_status(TestConnections *Test, const char *server, const char *status)
{
    char cmd[256];
    char maxadmin_result[1024];

    sprintf(cmd, "show server %s", server);
    Test->set_timeout(120);
    Test->maxscales->get_maxadmin_param(0, cmd, (char *) "Status:", maxadmin_result);
    if (maxadmin_result == NULL)
    {
        Test->add_result(1, "maxadmin execution error\n");
        return;
    }
    if (strstr(maxadmin_result, status)  == NULL )
    {
        Test->add_result(1, "Test failed, server '%s' status is '%s', expected '%s'\n", server, maxadmin_result,
                         status);
    }
}

void check_group(TestConnections *Test, const char *server, const char *group)
{

    int exit_code;
    char *output = Test->maxscales->ssh_node_output(0, "maxadmin show monitor MySQL-Monitor", true, &exit_code);

    if (output == NULL)
    {
        Test->add_result(1, "maxadmin execution error\n");
        return;
    }

    char *start = strstr(output, server);
    if (start == NULL)
    {
        Test->add_result(1, "maxadmin execution error\n");
        return;
    }
    char *value = strstr(start, "Master group");
    if (value == NULL)
    {
        Test->add_result(1, "maxadmin execution error\n");
        return;
    }

    value = strchr(value, ':') + 1;
    while (isspace(*value))
    {
        value++;
    }

    char *end = value;

    while (!isspace(*end))
    {
        end++;
    }

    *end = '\0';

    Test->add_result(strcmp(group, value), "Server '%s', expected group '%s', not '%s'", server, group, value);
}

void change_master(TestConnections *Test, int slave, int master)
{
    execute_query(Test->repl->nodes[slave], "CHANGE MASTER TO master_host='%s', master_port=3306, "
                  "master_log_file='mar-bin.000001', master_log_pos=310, master_user='repl', master_password='repl';START SLAVE",
                  Test->repl->IP[master], Test->repl->user_name, Test->repl->password);
}

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);

    Test->tprintf("Test 1 - Configure all servers into a multi-master ring with one slave");

    Test->set_timeout(120);
    Test->repl->execute_query_all_nodes("STOP SLAVE; RESET SLAVE ALL; RESET MASTER; SET GLOBAL read_only='OFF'");
    Test->repl->connect();
    change_master(Test, 0, 1);
    change_master(Test, 1, 2);
    change_master(Test, 2, 0);
    change_master(Test, 3, 2);

    sleep(2);

    check_status(Test, "server1", "Master, Running");
    check_status(Test, "server2", "Master, Running");
    check_status(Test, "server3", "Master, Running");
    check_status(Test, "server4", "Slave, Running");
    check_group(Test, "server1", "1");
    check_group(Test, "server2", "1");
    check_group(Test, "server3", "1");
    check_group(Test, "server4", "0");

    Test->tprintf("Test 2 - Set nodes 0 and 1 into read-only mode");

    Test->set_timeout(120);
    execute_query(Test->repl->nodes[0], "SET GLOBAL read_only='ON'");
    execute_query(Test->repl->nodes[1], "SET GLOBAL read_only='ON'");

    sleep(2);

    check_status(Test, "server1", "Slave, Running");
    check_status(Test, "server2", "Slave, Running");
    check_status(Test, "server3", "Master, Running");
    check_status(Test, "server4", "Slave, Running");
    check_group(Test, "server1", "1");
    check_group(Test, "server2", "1");
    check_group(Test, "server3", "1");
    check_group(Test, "server4", "0");

    Test->tprintf("Test 3 - Configure nodes 1 and 2 into a master-master pair, make node 0 "
                  "a slave of node 1 and node 3 a slave of node 2");

    Test->set_timeout(120);
    Test->repl->execute_query_all_nodes("STOP SLAVE; RESET SLAVE ALL; RESET MASTER;SET GLOBAL read_only='OFF'");
    Test->repl->connect();
    change_master(Test, 0, 1);
    change_master(Test, 1, 2);
    change_master(Test, 2, 1);
    change_master(Test, 3, 2);

    sleep(2);

    check_status(Test, "server1", "Slave, Running");
    check_status(Test, "server2", "Master, Running");
    check_status(Test, "server3", "Master, Running");
    check_status(Test, "server4", "Slave, Running");
    check_group(Test, "server1", "0");
    check_group(Test, "server2", "1");
    check_group(Test, "server3", "1");
    check_group(Test, "server4", "0");

    Test->tprintf("Test 4 - Set node 1 into read-only mode");

    Test->set_timeout(120);
    execute_query(Test->repl->nodes[1], "SET GLOBAL read_only='ON'");

    sleep(2);

    check_status(Test, "server1", "Slave, Running");
    check_status(Test, "server2", "Slave, Running");
    check_status(Test, "server3", "Master, Running");
    check_status(Test, "server4", "Slave, Running");
    check_group(Test, "server1", "0");
    check_group(Test, "server2", "1");
    check_group(Test, "server3", "1");
    check_group(Test, "server4", "0");

    Test->tprintf("Test 5 - Create two distinct groups");

    Test->set_timeout(120);
    Test->repl->execute_query_all_nodes("STOP SLAVE; RESET SLAVE ALL; RESET MASTER;SET GLOBAL read_only='OFF'");
    Test->repl->connect();
    change_master(Test, 0, 1);
    change_master(Test, 1, 0);
    change_master(Test, 2, 3);
    change_master(Test, 3, 2);

    sleep(2);

    check_status(Test, "server1", "Master, Running");
    check_status(Test, "server2", "Master, Running");
    check_status(Test, "server3", "Master, Running");
    check_status(Test, "server4", "Master, Running");
    check_group(Test, "server1", "1");
    check_group(Test, "server2", "1");
    check_group(Test, "server3", "2");
    check_group(Test, "server4", "2");


    Test->tprintf("Test 6 - Set nodes 1 and 3 into read-only mode");

    Test->set_timeout(120);
    execute_query(Test->repl->nodes[1], "SET GLOBAL read_only='ON'");
    execute_query(Test->repl->nodes[3], "SET GLOBAL read_only='ON'");

    sleep(2);

    check_status(Test, "server1", "Master, Running");
    check_status(Test, "server2", "Slave, Running");
    check_status(Test, "server3", "Master, Running");
    check_status(Test, "server4", "Slave, Running");
    check_group(Test, "server1", "1");
    check_group(Test, "server2", "1");
    check_group(Test, "server3", "2");
    check_group(Test, "server4", "2");

    Test->repl->execute_query_all_nodes("STOP SLAVE; RESET SLAVE ALL; RESET MASTER;SET GLOBAL read_only='OFF';");
    Test->repl->connect();
    change_master(Test, 1, 0);
    change_master(Test, 2, 0);
    change_master(Test, 3, 0);
    Test->repl->fix_replication();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
