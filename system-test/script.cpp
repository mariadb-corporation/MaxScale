/**
 * @file script.cpp - test for running external script feature (MXS-121)
 * - setup Maxscale to execute script on folowing events:
 *   - for MariaDB monitor: master_down,master_up, slave_up,   server_down
 * ,server_up,lost_master,lost_slave,new_master,new_slave
 *   - for Galera monitor: events=master_down,master_up, slave_up,   server_down
 * ,server_up,lost_master,lost_slave,new_master,new_slave,server_down,server_up,synced_down,synced_up
 * - for Galera monitor set also 'disable_master_role_setting=true'
 * - block master, unblock master, block node1, unblock node1
 * - expect following as a script output:
 * @verbatim
 *  --event=master_down --initiator=server1_IP:port
 *--nodelist=server1_IP:port,server2_IP:port,server3_IP:port,server4_IP:port
 *  --event=master_up --initiator=server1_IP:port
 *--nodelist=server1_IP:port,server2_IP:port,server3_IP:port,server4_IP:port
 *  --event=slave_up --initiator=server2_IP:port
 *--nodelist=server1_IP:port,server2_IP:port,server3_IP:port,server4_IP:port
 *  @endverbatim
 * - repeat test for Galera monitor: block node0, unblock node0, block node1, unblock node1
 * - expect following as a script output:
 * @verbatim
 *  --event=synced_down --initiator=gserver1_IP:port
 *--nodelist=gserver1_IP:port,gserver2_IP:port,gserver3_IP:port,gserver4_IP:port
 *  --event=synced_down --initiator=gserver2_IP:port
 *--nodelist=gserver1_IP:port,gserver2_IP:port,gserver3_IP:port,gserver4_IP:port
 *  --event=synced_up --initiator=gserver2_IP:port
 *--nodelist=gserver1_IP:port,gserver2_IP:port,gserver3_IP:port,gserver4_IP:port
 *  @endverbatim
 * - make script non-executable
 * - block and unblock node1
 * - check error log for 'The file cannot be executed: /home/$maxscales->access_user[0]/script.sh' error
 * - check if Maxscale still alive
 */


#include <iostream>
#include <maxtest/testconnections.hh>
#include <maxtest/galera_cluster.hh>

void test_script_monitor(TestConnections* Test, MariaDBCluster* nodes, char* expected_filename)
{
    Test->reset_timeout();
    auto homedir = Test->maxscale->access_homedir();
    Test->maxscale->ssh_node_f(true,
                               "cd %s; truncate -s 0 script_output; \
                                chown maxscale:maxscale script_output; \
                                chmod a+rw script_output",
                               homedir);
    sleep(10);

    Test->tprintf("Block master node");
    nodes->block_node(0);

    Test->tprintf("Sleeping");
    sleep(10);

    Test->tprintf("Unblock master node");
    nodes->unblock_node(0);

    Test->tprintf("Sleeping");
    sleep(10);

    Test->tprintf("Block node1");
    nodes->block_node(1);

    Test->tprintf("Sleeping");
    sleep(10);

    Test->tprintf("Unblock node1");
    nodes->unblock_node(1);

    Test->tprintf("Sleeping");
    sleep(10);

    Test->tprintf("Comparing results");

    if (Test->maxscale->ssh_node_f(false, "diff %s/script_output %s", homedir, expected_filename) != 0)
    {
        Test->maxscale->ssh_node_f(true, "cat %s/script_output", homedir);
        Test->add_result(1, "Wrong script output!");
    }
    else
    {
        Test->tprintf("Script output is OK!");
    }
}

int main(int argc, char* argv[])
{
    TestConnections::require_galera(true);
    TestConnections* Test = new TestConnections(argc, argv);
    Test->reset_timeout();

    auto repl_ip0 = Test->repl->ip_private(0);
    auto repl_ip1 = Test->repl->ip_private(1);
    auto repl_ip2 = Test->repl->ip_private(2);
    auto repl_ip3 = Test->repl->ip_private(3);

    auto gal_ip0 = Test->galera->ip_private(0);
    auto gal_ip1 = Test->galera->ip_private(1);
    auto gal_ip2 = Test->galera->ip_private(2);
    auto gal_ip3 = Test->galera->ip_private(3);

    auto homedir = Test->maxscale->access_homedir();
    auto sudo = Test->maxscale->access_sudo();

    Test->tprintf("Creating script on Maxscale machine");
    Test->maxscale->ssh_node_f(false,
                               "%s rm -rf %s/script; mkdir %s/script; "
                               "echo \"echo \\$* >> %s/script_output\" > %s/script/script.sh; "
                               "chmod a+x %s/script/script.sh; chmod a+x %s; "
                               "%s chown maxscale:maxscale %s/script -R",
                               sudo, homedir, homedir,
                               homedir, homedir,
                               homedir, homedir,
                               sudo, homedir);

    Test->maxscale->restart_maxscale();

    FILE* f = fopen("script_output_expected", "w");
    fprintf(f,
            "--event=master_down --initiator=[%s]:%d --nodelist=[%s]:%d,[%s]:%d,[%s]:%d\n",
            repl_ip0,
            Test->repl->port[0],
            repl_ip1,
            Test->repl->port[1],
            repl_ip2,
            Test->repl->port[2],
            repl_ip3,
            Test->repl->port[3]);
    fprintf(f,
            "--event=master_up --initiator=[%s]:%d --nodelist=[%s]:%d,[%s]:%d,[%s]:%d,[%s]:%d\n",
            repl_ip0,
            Test->repl->port[0],
            repl_ip0,
            Test->repl->port[0],
            repl_ip1,
            Test->repl->port[1],
            repl_ip2,
            Test->repl->port[2],
            repl_ip3,
            Test->repl->port[3]);
    fprintf(f,
            "--event=slave_up --initiator=[%s]:%d --nodelist=[%s]:%d,[%s]:%d,[%s]:%d,[%s]:%d\n",
            repl_ip1,
            Test->repl->port[1],
            repl_ip0,
            Test->repl->port[0],
            repl_ip1,
            Test->repl->port[1],
            repl_ip2,
            Test->repl->port[2],
            repl_ip3,
            Test->repl->port[3]);
    fclose(f);

    f = fopen("script_output_expected_galera", "w");
    fprintf(f,
            "--event=synced_down --initiator=[%s]:%d --nodelist=[%s]:%d,[%s]:%d,[%s]:%d\n",
            gal_ip0,
            Test->galera->port[0],
            gal_ip1,
            Test->galera->port[1],
            gal_ip2,
            Test->galera->port[2],
            gal_ip3,
            Test->galera->port[3]);
    fprintf(f,
            "--event=synced_up --initiator=[%s]:%d --nodelist=[%s]:%d,[%s]:%d,[%s]:%d,[%s]:%d\n",
            gal_ip0,
            Test->galera->port[0],
            gal_ip0,
            Test->galera->port[0],
            gal_ip1,
            Test->galera->port[1],
            gal_ip2,
            Test->galera->port[2],
            gal_ip3,
            Test->galera->port[3]);
    fprintf(f,
            "--event=synced_down --initiator=[%s]:%d --nodelist=[%s]:%d,[%s]:%d,[%s]:%d\n",
            gal_ip1,
            Test->galera->port[1],
            gal_ip0,
            Test->galera->port[0],
            gal_ip2,
            Test->galera->port[2],
            gal_ip3,
            Test->galera->port[3]);
    fprintf(f,
            "--event=synced_up --initiator=[%s]:%d --nodelist=[%s]:%d,[%s]:%d,[%s]:%d,[%s]:%d\n",
            gal_ip1,
            Test->galera->port[1],
            gal_ip0,
            Test->galera->port[0],
            gal_ip1,
            Test->galera->port[1],
            gal_ip2,
            Test->galera->port[2],
            gal_ip3,
            Test->galera->port[3]);
    fclose(f);

    Test->tprintf("Copying expected script output to Maxscale machine");
    char str[4096 + 2048];
    sprintf(str,
            "scp -i %s -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no  -o LogLevel=quiet script_output_expected* %s@%s:%s/",
            Test->maxscale->sshkey(),
            Test->maxscale->access_user(),
            Test->maxscale->ip4(),
            homedir);
    Test->add_result(system(str), "Error copying script to VM");

    sprintf(str, "%s/script_output_expected", homedir);
    test_script_monitor(Test, Test->repl, str);
    sprintf(str, "%s/script_output_expected_galera", homedir);
    test_script_monitor(Test, Test->galera, str);

    Test->reset_timeout();

    Test->tprintf("Making script non-executable");
    Test->maxscale->ssh_node_f(true, "chmod a-x %s/script/script.sh", homedir);

    sleep(3);

    Test->tprintf("Block node1");
    Test->repl->block_node(1);

    Test->tprintf("Sleeping");
    sleep(10);

    Test->tprintf("Unblock node1");
    Test->repl->unblock_node(1);

    sleep(15);

    Test->tprintf("Cheching Maxscale logs");
    Test->log_includes("Cannot execute file");

    Test->tprintf("checking if Maxscale is alive");
    Test->check_maxscale_alive();

    int rval = Test->global_result;
    delete Test;
    return rval;
}
