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
 * - check error log for 'Error: The file cannot be executed: /home/ec2-user/script.sh' error
 * - check if Maxscale still alive
 */


#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int test_script_monitor(TestConnections* Test, Mariadb_nodes* nodes, char * expected_filename)
{
    int global_result = 0;
    char str[256];

    sprintf(str, "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@%s 'rm /home/ec2-user/script_output'", Test->maxscale_sshkey, Test->maxscale_IP);
    system(str);

    sleep(30);

    printf("Block master node\n"); fflush(stdout);
    nodes->block_node(0);

    printf("Sleeping\n"); fflush(stdout);
    sleep(30);

    printf("Unblock master node\n"); fflush(stdout);
    Test->repl->unblock_node(0);

    printf("Sleeping\n"); fflush(stdout);
    sleep(30);

    printf("Block node1\n"); fflush(stdout);
    nodes->block_node(1);

    printf("Sleeping\n"); fflush(stdout);
    sleep(30);

    printf("Unblock node1\n"); fflush(stdout);
    nodes->unblock_node(1);

    printf("Sleeping\n"); fflush(stdout);
    sleep(30);

    printf("Printf results\n"); fflush(stdout);
    sprintf(str, "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@%s 'cat /home/ec2-user/script_output'", Test->maxscale_sshkey, Test->maxscale_IP);
    system(str);

    printf("Comparing results\n"); fflush(stdout);
    sprintf(str, "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@%s 'diff /home/ec2-user/script_output %s'", Test->maxscale_sshkey, Test->maxscale_IP, expected_filename);
    if (system(str) != 0) {
        printf("FAIL! Wrong script output!\n");
        global_result++;
    } else {
        printf("Script output is OK!\n");
    }
    return(global_result);
}

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;
    int i;
    char str[256];

    Test->read_env();
    Test->print_env();

    printf("Creating script on Maxscale machine\n"); fflush(stdout);

    sprintf(str, "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@%s 'echo \"echo \\$* >> /home/ec2-user/script_output\" > /home/ec2-user/script.sh; chmod a+x /home/ec2-user/script.sh'", Test->maxscale_sshkey, Test->maxscale_IP);
    system(str);

    Test->restart_maxscale();

    FILE * f;
    f = fopen("script_output_expected", "w");
    fprintf(f, "--event=master_down --initiator=%s:%d --nodelist=%s:%d,%s:%d,%s:%d,%s:%d", Test->repl->IP[0], Test->repl->port[0], Test->repl->IP[0], Test->repl->port[0], Test->repl->IP[1], Test->repl->port[1], Test->repl->IP[2], Test->repl->port[2], Test->repl->IP[3], Test->repl->port[3]);
    fprintf(f, "--event=master_up --initiator=%s:%d --nodelist=%s:%d,%s:%d,%s:%d,%s:%d", Test->repl->IP[0], Test->repl->port[0], Test->repl->IP[0], Test->repl->port[0], Test->repl->IP[1], Test->repl->port[1], Test->repl->IP[2], Test->repl->port[2], Test->repl->IP[3], Test->repl->port[3]);
    fprintf(f, "--event=slave_up --initiator=%s:%d --nodelist=%s:%d,%s:%d,%s:%d,%s:%d", Test->repl->IP[1], Test->repl->port[1], Test->repl->IP[0], Test->repl->port[0], Test->repl->IP[1], Test->repl->port[1], Test->repl->IP[2], Test->repl->port[2], Test->repl->IP[3], Test->repl->port[3]);
    fclose(f);

    f = fopen("script_output_expected_galera", "w");
    fprintf(f, "--event=synced_down --initiator=%s:%d --nodelist=%s:%d,%s:%d,%s:%d,%s:%d", Test->galera->IP[0], Test->galera->port[0], Test->galera->IP[0], Test->galera->port[0], Test->galera->IP[1], Test->galera->port[1], Test->galera->IP[2], Test->galera->port[2], Test->galera->IP[3], Test->galera->port[3]);
    fprintf(f, "--event=synced_up --initiator=%s:%d --nodelist=%s:%d,%s:%d,%s:%d,%s:%d", Test->galera->IP[0], Test->galera->port[0], Test->galera->IP[0], Test->galera->port[0], Test->galera->IP[1], Test->galera->port[1], Test->galera->IP[2], Test->galera->port[2], Test->galera->IP[3], Test->galera->port[3]);
    fprintf(f, "--event=synced_down --initiator=%s:%d --nodelist=%s:%d,%s:%d,%s:%d,%s:%d", Test->galera->IP[1], Test->galera->port[1], Test->galera->IP[0], Test->galera->port[0], Test->galera->IP[1], Test->galera->port[1], Test->galera->IP[2], Test->galera->port[2], Test->galera->IP[3], Test->galera->port[3]);
    fprintf(f, "--event=synced_up --initiator=%s:%d --nodelist=%s:%d,%s:%d,%s:%d,%s:%d", Test->galera->IP[1], Test->galera->port[1], Test->galera->IP[0], Test->galera->port[0], Test->galera->IP[1], Test->galera->port[1], Test->galera->IP[2], Test->galera->port[2], Test->galera->IP[3], Test->galera->port[3]);
    fclose(f);

    printf("Copying expected script output to Maxscale machine\n"); fflush(stdout);
    sprintf(str, "scp -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no %s/script_output_expected* root@%s:/home/ec2-user/", Test->maxscale_sshkey, Test->test_dir, Test->maxscale_IP);
    system(str);

    global_result += test_script_monitor(Test, Test->repl, (char *) "script_output_expected");
    global_result += test_script_monitor(Test, Test->galera, (char *) "script_output_expected");

    printf("Making script non-executable\n"); fflush(stdout);
    sprintf(str, "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@%s 'chmod a-x /home/ec2-user/script.sh'", Test->maxscale_sshkey, Test->maxscale_IP);
    system(str);

    sleep(3);

    printf("Block node1\n"); fflush(stdout);
    Test->repl->block_node(1);

    printf("Sleeping\n"); fflush(stdout);
    sleep(10);

    printf("Unblock node1\n"); fflush(stdout);
    Test->repl->unblock_node(1);

    sleep(15);
    global_result += check_log_err((char *) "Error: Cannot execute file: /home/ec2-user/script.sh", true);

    printf("checking if Maxscale is alive\n"); fflush(stdout);
    global_result += check_maxscale_alive();

    Test->copy_all_logs(); return(global_result);
}
