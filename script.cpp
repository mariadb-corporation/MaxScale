/**
 * @file script.cpp - test for running external script feature (MXS-121)
 * - setup Maxscale to execute script on folowing events:
 *   - for MariaDB monitor: master_down,master_up, slave_up,   server_down  ,server_up,lost_master,lost_slave,new_master,new_slave
 *   - for Galera monitor: events=master_down,master_up, slave_up,   server_down  ,server_up,lost_master,lost_slave,new_master,new_slave,server_down,server_up,synced_down,synced_up
 * - for Galera monitor set also 'disable_master_role_setting=true'
 * - block master, unblock master, block node1, unblock node1
 * - expect following as a script output:
 * @verbatim
--event=master_down --initiator=server1_IP:port --nodelist=server1_IP:port,server2_IP:port,server3_IP:port,server4_IP:port
--event=master_up --initiator=server1_IP:port --nodelist=server1_IP:port,server2_IP:port,server3_IP:port,server4_IP:port
--event=slave_up --initiator=server2_IP:port --nodelist=server1_IP:port,server2_IP:port,server3_IP:port,server4_IP:port
@endverbatim
 * - repeat test for Galera monitor: block node0, unblock node0, block node1, unblock node1
 * - expect following as a script output:
 * @verbatim
--event=synced_down --initiator=gserver1_IP:port --nodelist=gserver1_IP:port,gserver2_IP:port,gserver3_IP:port,gserver4_IP:port
--event=synced_down --initiator=gserver2_IP:port --nodelist=gserver1_IP:port,gserver2_IP:port,gserver3_IP:port,gserver4_IP:port
--event=synced_up --initiator=gserver2_IP:port --nodelist=gserver1_IP:port,gserver2_IP:port,gserver3_IP:port,gserver4_IP:port
 @endverbatim
 * - make script non-executable
 * - block and unblocm node1
 * - check error log for 'The file cannot be executed: /home/$maxscale_access_user/script.sh' error
 * - check if Maxscale still alive
 */


#include <my_config.h>
#include <iostream>
#include "testconnections.h"

void test_script_monitor(TestConnections* Test, Mariadb_nodes* nodes, char * expected_filename)
{
    char str[1024];
    Test->set_timeout(200);

    sprintf(str, "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no  -o LogLevel=quiet %s@%s 'rm %s/script_output'", Test->maxscale_sshkey, Test->maxscale_access_user, Test->maxscale_IP, Test->maxscale_access_homedir);
    system(str);

    sprintf(str, "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no  -o LogLevel=quiet %s@%s '%s touch %s/script_output; %s chown maxscale:maxscale %s/script_output'",
            Test->maxscale_sshkey, Test->maxscale_access_user, Test->maxscale_IP, Test->maxscale_access_sudo,
            Test->maxscale_access_homedir, Test->maxscale_access_sudo, Test->maxscale_access_homedir);
    Test->tprintf("%s\n", str);fflush(stdout);
    system(str);

    sleep(30);

    Test->tprintf("Block master node\n");
    nodes->block_node(0);

    Test->tprintf("Sleeping\n");
    sleep(30);

    Test->tprintf("Unblock master node\n");
    nodes->unblock_node(0);

    Test->tprintf("Sleeping\n");
    sleep(30);

    Test->tprintf("Block node1\n");
    nodes->block_node(1);

    Test->tprintf("Sleeping\n");
    sleep(30);

    Test->tprintf("Unblock node1\n");
    nodes->unblock_node(1);

    Test->tprintf("Sleeping\n");
    sleep(30);

    Test->tprintf("Printf results\n");
    sprintf(str, "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no  -o LogLevel=quiet %s@%s 'cat %s/script_output'",
            Test->maxscale_sshkey, Test->maxscale_access_user, Test->maxscale_IP, Test->maxscale_access_homedir);
    system(str);

    Test->tprintf("Comparing results\n");
    sprintf(str, "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no  -o LogLevel=quiet %s@%s 'diff %s/script_output %s'",
            Test->maxscale_sshkey, Test->maxscale_access_user, Test->maxscale_IP, Test->maxscale_access_homedir, expected_filename);
    Test->tprintf("%s\n", str);
    if (system(str) != 0) {
        Test->add_result(1, "Wrong script output!\n");
    } else {
        Test->tprintf("Script output is OK!\n");
    }
}

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    Test->set_timeout(100);
    int i;
    char str[1024];

    Test->tprintf("Creating script on Maxscale machine\n");


    sprintf(str, "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no  -o LogLevel=quiet %s@%s \
            '%s rm -rf %s/script; mkdir %s/script; echo \"echo \\$* >> %s/script_output\" > %s/script/script.sh; \
            chmod a+x %s/script/script.sh; chmod a+x %s; %s chown maxscale:maxscale %s/script -R'",
            Test->maxscale_sshkey, Test->maxscale_access_user, Test->maxscale_IP, Test->maxscale_access_sudo, Test->maxscale_access_homedir,
            Test->maxscale_access_homedir, Test->maxscale_access_homedir,
            Test->maxscale_access_homedir, Test->maxscale_access_homedir, Test->maxscale_access_homedir, Test->maxscale_access_sudo,
            Test->maxscale_access_homedir);
    Test->tprintf("%s\n", str);
    system(str);

    Test->restart_maxscale();

    FILE * f;
    f = fopen("script_output_expected", "w");
    fprintf(f, "--event=master_down --initiator=%s:%d --nodelist=%s:%d,%s:%d,%s:%d,%s:%d\n",
            Test->repl->IP_private[0], Test->repl->port[0], Test->repl->IP_private[0],
            Test->repl->port[0], Test->repl->IP_private[1], Test->repl->port[1],
            Test->repl->IP_private[2], Test->repl->port[2], Test->repl->IP_private[3],
            Test->repl->port[3]);
    fprintf(f, "--event=master_up --initiator=%s:%d --nodelist=%s:%d,%s:%d,%s:%d,%s:%d\n",
            Test->repl->IP_private[0], Test->repl->port[0], Test->repl->IP_private[0],
            Test->repl->port[0], Test->repl->IP_private[1], Test->repl->port[1],
            Test->repl->IP_private[2], Test->repl->port[2], Test->repl->IP_private[3],
            Test->repl->port[3]);
    fprintf(f, "--event=slave_up --initiator=%s:%d --nodelist=%s:%d,%s:%d,%s:%d,%s:%d\n",
            Test->repl->IP_private[1], Test->repl->port[1], Test->repl->IP_private[0],
            Test->repl->port[0], Test->repl->IP_private[1], Test->repl->port[1],
            Test->repl->IP_private[2], Test->repl->port[2], Test->repl->IP_private[3],
            Test->repl->port[3]);
    fclose(f);

    f = fopen("script_output_expected_galera", "w");
    fprintf(f, "--event=synced_down --initiator=%s:%d --nodelist=%s:%d,%s:%d,%s:%d,%s:%d\n",
            Test->galera->IP_private[0], Test->galera->port[0], Test->galera->IP_private[0],
            Test->galera->port[0], Test->galera->IP_private[1], Test->galera->port[1],
            Test->galera->IP_private[2], Test->galera->port[2], Test->galera->IP_private[3],
            Test->galera->port[3]);
    fprintf(f, "--event=synced_up --initiator=%s:%d --nodelist=%s:%d,%s:%d,%s:%d,%s:%d\n",
            Test->galera->IP_private[0], Test->galera->port[0], Test->galera->IP_private[0],
            Test->galera->port[0], Test->galera->IP_private[1], Test->galera->port[1],
            Test->galera->IP_private[2], Test->galera->port[2], Test->galera->IP_private[3],
            Test->galera->port[3]);
    fprintf(f, "--event=synced_down --initiator=%s:%d --nodelist=%s:%d,%s:%d,%s:%d,%s:%d\n",
            Test->galera->IP_private[1], Test->galera->port[1], Test->galera->IP_private[0],
            Test->galera->port[0], Test->galera->IP_private[1], Test->galera->port[1],
            Test->galera->IP_private[2], Test->galera->port[2], Test->galera->IP_private[3],
            Test->galera->port[3]);
    fprintf(f, "--event=synced_up --initiator=%s:%d --nodelist=%s:%d,%s:%d,%s:%d,%s:%d\n",
            Test->galera->IP_private[1], Test->galera->port[1], Test->galera->IP_private[0],
            Test->galera->port[0], Test->galera->IP_private[1], Test->galera->port[1],
            Test->galera->IP_private[2], Test->galera->port[2], Test->galera->IP_private[3],
            Test->galera->port[3]);
    fclose(f);

    Test->tprintf("Copying expected script output to Maxscale machine\n");
    sprintf(str, "scp -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no  -o LogLevel=quiet script_output_expected* %s@%s:%s/",
            Test->maxscale_sshkey, Test->maxscale_access_user, Test->maxscale_IP, Test->maxscale_access_homedir);
    system(str);

    sprintf(str, "%s/script_output_expected", Test->maxscale_access_homedir);
    test_script_monitor(Test, Test->repl, str);
    sprintf(str, "%s/script_output_expected_galera", Test->maxscale_access_homedir);
    test_script_monitor(Test, Test->galera, str);

    Test->set_timeout(200);

    Test->tprintf("Making script non-executable\n");
    sprintf(str, "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no  -o LogLevel=quiet %s@%s '%s chmod a-x %s/script/script.sh'",
            Test->maxscale_sshkey, Test->maxscale_access_user, Test->maxscale_IP, Test->maxscale_access_sudo,
            Test->maxscale_access_homedir);
    system(str);

    sleep(3);

    Test->tprintf("Block node1\n");
    Test->repl->block_node(1);

    Test->tprintf("Sleeping\n");
    sleep(10);

    Test->tprintf("Unblock node1\n");
    Test->repl->unblock_node(1);

    sleep(15);

    Test->tprintf("Cheching Maxscale logs\n");
    Test->check_log_err((char *) "Cannot execute file" , true);
    //Test->check_log_err((char *) "Missing execution permissions" , true);fflush(stdout);

    Test->tprintf("checking if Maxscale is alive\n");
    Test->check_maxscale_alive();

    Test->copy_all_logs(); return(Test->global_result);
}
