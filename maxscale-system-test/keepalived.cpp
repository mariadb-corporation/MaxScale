/**
 * @file keepalived.cpp keepalived Test of two Maxscale + keepalived failover
 *
 * - 'version_string' configured to be different for every Maxscale
 * - configure keepalived for two nodes (uses xxx.xxx.xxx.253 as a virtual IP
 * where xxx.xxx.xxx. - first 3 numbers from client IP)
 * - suspend Maxscale 1
 * - wait and check version_string from Maxscale on virtual IP, expect 10.2-server2
 * - resume Maxscale 1, suspend Maxscale 2
 * - wait and check version_string from Maxscale on virtual IP, expect 10.2-server1
 * - resume Maxscale 2
 * TODO: replace 'yum' call with executing Chef recipe
 */


#include <iostream>
#include "testconnections.h"

#define FAILOVER_WAIT_TIME 5

char virtual_ip[16];
char * print_version_string(TestConnections * Test)
{
    MYSQL * keepalived_conn = open_conn(Test->maxscales->rwsplit_port[0], virtual_ip, Test->maxscales->user_name, Test->maxscales->password, Test->ssl);
    const char * version_string;
    mariadb_get_info(keepalived_conn, MARIADB_CONNECTION_SERVER_VERSION, (void *)&version_string);
    Test->tprintf("%s\n", version_string);
    mysql_close(keepalived_conn);
    return((char*) version_string);
}

int main(int argc, char *argv[])
{
    int i;
    char * version;

    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(10);

    Test->tprintf("Maxscale_N %d\n", Test->maxscales->N);
    if (Test->maxscales->N < 2)
    {
        Test->tprintf("At least 2 Maxscales are needed for this test. Exiting\n");
        exit(0);
    }


    Test->check_maxscale_alive(0);
    Test->check_maxscale_alive(1);

    // Get test client IP, replace last number in it with 253 and use it as Virtual IP
    char client_ip[24];
    char * last_dot;
    Test->get_client_ip(0, client_ip);
    last_dot = client_ip;
    Test->tprintf("My IP is %s\n", client_ip);
    for (i = 0; i < 3; i++)
    {
        last_dot = strstr(last_dot, ".");
        last_dot = &last_dot[1];
    }
    last_dot[0] = '\0';
    Test->tprintf("First part of IP is %s\n", client_ip);

    sprintf(virtual_ip, "%s253", client_ip);


    for (i = 0; i < Test->maxscales->N; i++)
    {
        std::string src = std::string(test_dir) + "/keepalived_cnf/" + std::to_string(i + 1) + ".conf";
        std::string cp_cmd = "cp " + std::string(Test->maxscales->access_homedir[i]) + std::to_string(i + 1) + ".conf " +
                " /etc/keepalived/keepalived.conf";
        Test->tprintf("%s\n", src.c_str());
        Test->tprintf("%s\n", cp_cmd.c_str());
        Test->maxscales->ssh_node(i, "yum install -y keepalived", true);
        Test->maxscales->copy_to_node(i, src.c_str(), Test->maxscales->access_homedir[i]);
        Test->maxscales->ssh_node(i, cp_cmd.c_str(), true);
        Test->maxscales->ssh_node_f(i, true, "sed -i \"s/###virtual_ip###/%s/\" /etc/keepalived/keepalived.conf", virtual_ip);
        std::string script_src = std::string(test_dir) + "/keepalived_cnf/is_maxscale_running.sh";
        std::string script_cp_cmd = "cp " + std::string(Test->maxscales->access_homedir[i]) + "is_maxscale_running.sh /usr/bin/";
        Test->maxscales->copy_to_node(i, script_src.c_str(), Test->maxscales->access_homedir[i]);
        Test->maxscales->ssh_node(i, script_cp_cmd.c_str(), true);
        Test->maxscales->ssh_node(i, "sudo service keepalived restart", true);
    }

    print_version_string(Test);

    Test->tprintf("Suspend Maxscale 000 machine and waiting\n");
    system(Test->maxscales->stop_vm_command[0]);
    sleep(FAILOVER_WAIT_TIME);

    version = print_version_string(Test);
    if (strcmp(version, "10.2-server2") != 0)
    {
        Test->add_result(false, "Failover did not happen");
    }


    Test->tprintf("Resume Maxscale 000 machine and waiting\n");
    system(Test->maxscales->start_vm_command[0]);
    sleep(FAILOVER_WAIT_TIME);
    print_version_string(Test);

    Test->tprintf("Suspend Maxscale 001 machine and waiting\n");
    system(Test->maxscales->stop_vm_command[1]);
    sleep(FAILOVER_WAIT_TIME);

    version = print_version_string(Test);
    if (strcmp(version, "10.2-server1") != 0)
    {
        Test->add_result(false, "Failover did not happen");
    }

    print_version_string(Test);
    Test->tprintf("Resume Maxscale 001 machine and waiting\n");
    system(Test->maxscales->start_vm_command[1]);
    sleep(FAILOVER_WAIT_TIME);
    print_version_string(Test);

    Test->tprintf("Stop Maxscale on 000 machine\n");
    Test->stop_maxscale(0);
    sleep(FAILOVER_WAIT_TIME);
    version = print_version_string(Test);
    if (strcmp(version, "10.2-server2") != 0)
    {
        Test->add_result(false, "Failover did not happen");
    }

    Test->tprintf("Start back Maxscale on 000 machine\n");
    Test->start_maxscale(0);
    sleep(FAILOVER_WAIT_TIME);

    Test->tprintf("Stop Maxscale on 001 machine\n");
    Test->stop_maxscale(1);
    sleep(FAILOVER_WAIT_TIME);
    version = print_version_string(Test);
    if (strcmp(version, "10.2-server1") != 0)
    {
        Test->add_result(false, "Failover did not happen");
    }

    int rval = Test->global_result;
    delete Test;
    return rval;
}

