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


#include <maxtest/testconnections.hh>
#include <maxtest/galera_cluster.hh>
#include <maxbase/format.hh>

using std::string;
const int script_delay_ticks = 2;

void test_script_monitor(TestConnections& test, MariaDBCluster* nodes, const char* expected_filename)
{
    test.reset_timeout();
    auto& mxs = *test.maxscale;
    auto homedir = mxs.access_homedir();
    mxs.ssh_node_f(true,
                   "cd %s; truncate -s 0 script_output; chown maxscale:maxscale script_output; "
                   "chmod a+rw script_output",
                   homedir);

    mxs.wait_for_monitor(script_delay_ticks);

    test.tprintf("Block master node");
    nodes->block_node(0);
    mxs.wait_for_monitor(script_delay_ticks);

    test.tprintf("Unblock master node");
    nodes->unblock_node(0);
    mxs.wait_for_monitor(script_delay_ticks);

    test.tprintf("Block node1");
    nodes->block_node(1);
    mxs.wait_for_monitor(script_delay_ticks);

    test.tprintf("Unblock node1");
    nodes->unblock_node(1);
    mxs.wait_for_monitor(script_delay_ticks);

    test.tprintf("Comparing results");

    if (mxs.ssh_node_f(false, "diff %s/script_output %s", homedir, expected_filename) != 0)
    {
        mxs.ssh_node_f(true, "cat %s/script_output", homedir);
        test.add_failure("Wrong script output!");
    }
    else
    {
        test.tprintf("Script output is OK!");
    }
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    auto& repl = *test.repl;
    auto repl_ip0 = repl.ip_private(0);
    auto repl_ip1 = repl.ip_private(1);
    auto repl_ip2 = repl.ip_private(2);
    auto repl_ip3 = repl.ip_private(3);
    auto repl_port0 = repl.port[0];
    auto repl_port1 = repl.port[1];
    auto repl_port2 = repl.port[2];
    auto repl_port3 = repl.port[3];

    auto& galera = *test.galera;
    auto gal_ip0 = galera.ip_private(0);
    auto gal_ip1 = galera.ip_private(1);
    auto gal_ip2 = galera.ip_private(2);
    auto gal_ip3 = galera.ip_private(3);
    auto gal_port0 = galera.port[0];
    auto gal_port1 = galera.port[1];
    auto gal_port2 = galera.port[2];
    auto gal_port3 = galera.port[3];

    auto& mxs = *test.maxscale;
    auto mxs_homedir = mxs.access_homedir();
    auto sudo = mxs.access_sudo();

    test.tprintf("Creating script on Maxscale machine");
    mxs.ssh_node_f(false,
                   "%s rm -rf %s/script; mkdir %s/script; "
                   "echo \"echo \\$* >> %s/script_output\" > %s/script/script.sh; "
                   "chmod a+x %s/script/script.sh; chmod a+x %s; "
                   "%s chown maxscale:maxscale %s/script -R",
                   sudo, mxs_homedir, mxs_homedir,
                   mxs_homedir, mxs_homedir,
                   mxs_homedir, mxs_homedir,
                   sudo, mxs_homedir);

    mxs.restart_maxscale();

    const char repl_script_outfile[] = "script_output_expected";
    FILE* f = fopen(repl_script_outfile, "w");
    fprintf(f, "--event=master_down --initiator=[%s]:%d --nodelist=[%s]:%d,[%s]:%d,[%s]:%d\n",
            repl_ip0, repl_port0,
            repl_ip1, repl_port1,
            repl_ip2, repl_port2,
            repl_ip3, repl_port3);

    fprintf(f, "--event=master_up --initiator=[%s]:%d --nodelist=[%s]:%d,[%s]:%d,[%s]:%d,[%s]:%d\n",
            repl_ip0, repl_port0,
            repl_ip0, repl_port0,
            repl_ip1, repl_port1,
            repl_ip2, repl_port2,
            repl_ip3, repl_port3);

    fprintf(f, "--event=slave_up --initiator=[%s]:%d --nodelist=[%s]:%d,[%s]:%d,[%s]:%d,[%s]:%d\n",
            repl_ip1, repl_port1,
            repl_ip0, repl_port0,
            repl_ip1, repl_port1,
            repl_ip2, repl_port2,
            repl_ip3, repl_port3);
    fclose(f);

    const char galera_script_outfile[] = "script_output_expected_galera";
    f = fopen(galera_script_outfile, "w");
    fprintf(f, "--event=synced_down --initiator=[%s]:%d --nodelist=[%s]:%d,[%s]:%d,[%s]:%d\n",
            gal_ip0, gal_port0,
            gal_ip1, gal_port1,
            gal_ip2, gal_port2,
            gal_ip3, gal_port3);
    fprintf(f, "--event=synced_up --initiator=[%s]:%d --nodelist=[%s]:%d,[%s]:%d,[%s]:%d,[%s]:%d\n",
            gal_ip0, gal_port0,
            gal_ip0, gal_port0,
            gal_ip1, gal_port1,
            gal_ip2, gal_port2,
            gal_ip3, gal_port3);
    fprintf(f, "--event=synced_down --initiator=[%s]:%d --nodelist=[%s]:%d,[%s]:%d,[%s]:%d\n",
            gal_ip1, gal_port1,
            gal_ip0, gal_port0,
            gal_ip2, gal_port2,
            gal_ip3, gal_port3);
    fprintf(f, "--event=synced_up --initiator=[%s]:%d --nodelist=[%s]:%d,[%s]:%d,[%s]:%d,[%s]:%d\n",
            gal_ip1, gal_port1,
            gal_ip0, gal_port0,
            gal_ip1, gal_port1,
            gal_ip2, gal_port2,
            gal_ip3, gal_port3);
    fclose(f);

    test.tprintf("Copying expected script output files to Maxscale machine.");
    mxs.copy_to_node(repl_script_outfile, mxs_homedir);
    mxs.copy_to_node(galera_script_outfile, mxs_homedir);

    if (test.ok())
    {
        string repl_script_outfile_path = string(mxs_homedir) + "/" + repl_script_outfile;
        test_script_monitor(test, &repl, repl_script_outfile_path.c_str());
        string galera_script_outfile_path = string(mxs_homedir) + "/" + galera_script_outfile;
        test_script_monitor(test, &galera, galera_script_outfile_path.c_str());

        test.reset_timeout();

        test.tprintf("Making script non-executable");
        mxs.ssh_node_f(true, "chmod a-x %s/script/script.sh", mxs_homedir);

        mxs.wait_for_monitor(script_delay_ticks);

        test.tprintf("Block node1");
        repl.block_node(1);
        mxs.wait_for_monitor(script_delay_ticks);

        test.tprintf("Unblock node1");
        repl.unblock_node(1);
        mxs.wait_for_monitor(script_delay_ticks);
    }

    test.log_includes("Cannot execute file");
    test.check_maxscale_alive();
    return test.global_result;
}
