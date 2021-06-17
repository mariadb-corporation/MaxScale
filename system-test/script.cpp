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

void test_main(TestConnections& test);
int main(int argc, char* argv[])
{
    TestConnections test;
    TestConnections::skip_maxscale_start(true);
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    // Generate some strings used repeatedly later on.
    const char fmt[] = "[%s]:%d";

    auto& repl = *test.repl;
    string repl0s = mxb::string_printf(fmt, repl.ip_private(0), repl.port[0]);
    string repl1s = mxb::string_printf(fmt, repl.ip_private(1), repl.port[1]);
    string repl2s = mxb::string_printf(fmt, repl.ip_private(2), repl.port[2]);
    string repl3s = mxb::string_printf(fmt, repl.ip_private(3), repl.port[3]);
    auto repl0 = repl0s.c_str();
    auto repl1 = repl1s.c_str();
    auto repl2 = repl2s.c_str();
    auto repl3 = repl3s.c_str();

    auto& galera = *test.galera;
    string gal0s = mxb::string_printf(fmt, galera.ip_private(0), galera.port[0]);
    string gal1s = mxb::string_printf(fmt, galera.ip_private(1), galera.port[1]);
    string gal2s = mxb::string_printf(fmt, galera.ip_private(2), galera.port[2]);
    string gal3s = mxb::string_printf(fmt, galera.ip_private(3), galera.port[3]);
    auto gal0 = gal0s.c_str();
    auto gal1 = gal1s.c_str();
    auto gal2 = gal2s.c_str();
    auto gal3 = gal3s.c_str();

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

    const char line_3up_fmt[] = "--event=%s --initiator=%s --nodelist=%s,%s,%s\n";
    const char line_4up_fmt[] = "--event=%s --initiator=%s --nodelist=%s,%s,%s,%s\n";

    const char repl_script_outfile[] = "script_output_expected";
    FILE* f = fopen(repl_script_outfile, "w");
    fprintf(f, line_3up_fmt, "master_down", repl0, repl1, repl2, repl3);
    fprintf(f, line_4up_fmt, "master_up", repl0, repl0, repl1, repl2, repl3);
    fprintf(f, line_4up_fmt, "slave_up", repl1, repl0, repl1, repl2, repl3);
    fclose(f);

    const char galera_script_outfile[] = "script_output_expected_galera";
    f = fopen(galera_script_outfile, "w");
    fprintf(f, line_3up_fmt, "synced_down", gal0, gal1, gal2, gal3);
    fprintf(f, line_4up_fmt, "synced_up", gal0, gal0, gal1, gal2, gal3);
    fprintf(f, line_3up_fmt, "synced_down", gal1, gal0, gal2, gal3);
    fprintf(f, line_4up_fmt, "synced_up", gal1, gal0, gal1, gal2, gal3);
    fclose(f);

    test.tprintf("Copying expected script output files to Maxscale machine.");
    mxs.copy_to_node(repl_script_outfile, mxs_homedir);
    mxs.copy_to_node(galera_script_outfile, mxs_homedir);

    mxs.start();

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
}
