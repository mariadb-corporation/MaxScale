/**
 * @file script.cpp - test for running external script feature (MXS-121)
 */


#include <my_config.h>
#include <iostream>
#include "testconnections.h"

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;
    int i;
    char str[256];

    Test->read_env();
    Test->print_env();

    printf("Creating script on Maxscale machine\n"); fflush(stdout);
    sprintf(str, "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@%s 'echo \"echo $* >> /home/ec2-user/script_output\" > /home/ec2-user/script.sh'", Test->maxscale_sshkey, Test->maxscale_IP);
    system(str);

    printf("Block master node\n"); fflush(stdout);
    Test->repl->block_node(0);

    printf("Sleeping\n"); fflush(stdout);
    sleep(30);

    printf("Unblock master node\n"); fflush(stdout);
    Test->repl->unblock_node(0);

    printf("Sleeping\n"); fflush(stdout);
    sleep(30);

    printf("Block node1\n"); fflush(stdout);
    Test->repl->block_node(1);

    printf("Sleeping\n"); fflush(stdout);
    sleep(30);

    printf("Unblock node1\n"); fflush(stdout);
    Test->repl->unblock_node(1);

    printf("Sleeping\n"); fflush(stdout);
    sleep(30);

    printf("Printf results\n"); fflush(stdout);
    sprintf(str, "ssh -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@%s 'cat /home/ec2-user/script_output'", Test->maxscale_sshkey, Test->maxscale_IP);
    system(str);


    Test->copy_all_logs(); return(global_result);
}
