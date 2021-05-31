#include <string>
#include <maxtest/testconnections.hh>
#include <maxtest/mariadb_connector.hh>

using std::string;
using std::vector;
using mxt::ServersInfo;
using mxt::ServerInfo;

int main(int argc, char* argv[])
{
    auto master_st = ServerInfo::MASTER | ServerInfo::RUNNING;
    auto slave_st = ServerInfo::SLAVE | ServerInfo::RUNNING;
    auto running_st = ServerInfo::RUNNING;
    auto down_st = ServerInfo::DOWN;

    const string mon_name = "MariaDB-Monitor";
    const string master_cond = "master_conditions";
    const string slave_cond = "slave_conditions";

    auto master_3slaves = {master_st, slave_st, slave_st, slave_st};
    auto master_3running = {master_st, running_st, running_st, running_st};
    auto slave_3running = {slave_st, running_st, running_st, running_st};
    auto down_3running = {down_st, running_st, running_st, running_st};
    auto down_3slaves = {down_st, slave_st, slave_st, slave_st};
    auto all_running = {running_st, running_st, running_st, running_st};

    TestConnections test(argc, argv);
    auto& mxs = test.maxscale->maxscale_b();

    auto reset = [&]() {
            test.repl->stop_slaves();
            mxs.alter_monitor(mon_name, master_cond, "none");
            test.repl->connect();
            test.repl->replicate_from(1, 0);
            test.repl->replicate_from(2, 0);
            test.repl->replicate_from(3, 0);
        };

    auto alter_monitor = [&](const string& setting, const string& value) {
            test.tprintf("Set %s=%s.", setting.c_str(), value.c_str());
            mxs.alter_monitor(mon_name, setting, value);
            mxs.wait_monitor_ticks();
        };

    auto check_io_connecting = [&test](const ServerInfo& srv_info) {
            if (srv_info.slave_connections.empty())
            {
                test.add_failure("'%s' has no slave connections", srv_info.name.c_str());
            }
            else
            {
                bool ok = srv_info.slave_connections[0].io_running
                    == ServerInfo::SlaveConnection::IO_State::CONNECTING;
                test.expect(ok, "Slave_IO_Running of '%s' is not 'Connecting'", srv_info.name.c_str());
            }
        };

    mxs.check_servers_status(master_3slaves);
    const int master_ind = 0;
    if (test.ok())
    {
        test.tprintf("Stop all slaves, first server should remain [Master].");
        test.repl->stop_slaves();
        mxs.wait_monitor_ticks();
        mxs.check_servers_status(master_3running);

        alter_monitor(master_cond, "connected_slave,running_slave");
        test.tprintf("Should lose [Master], but gain [Slave].");
        mxs.wait_monitor_ticks();
        mxs.check_servers_status(slave_3running);

        test.tprintf("Have one slave start replicating. Should regain [Master].");
        const int slave_ind = 1;
        test.repl->connect(slave_ind);
        test.repl->replicate_from(slave_ind, master_ind);
        mxs.wait_monitor_ticks();
        mxs.check_servers_status({master_st, slave_st});

        test.tprintf("Shut down the slave. Should lose [Master].");
        test.repl->stop_node(slave_ind);
        mxs.wait_monitor_ticks();
        mxs.check_servers_status({slave_st, down_st});
        test.repl->start_node(slave_ind);

        alter_monitor(master_cond, "connected_slave");
        test.tprintf("Stopping replication should lose [Master].");
        test.repl->connect(slave_ind);
        test.try_query(test.repl->nodes[slave_ind], "STOP SLAVE;");
        mxs.wait_monitor_ticks();
        mxs.check_servers_status(slave_3running);

        test.tprintf("Restart replication, then shut down slave. Should keep [Master].");
        test.try_query(test.repl->nodes[slave_ind], "START SLAVE;");
        mxs.wait_monitor_ticks();
        test.repl->stop_node(slave_ind);
        mxs.wait_monitor_ticks();
        mxs.check_servers_status({master_st, down_st});
        test.repl->start_node(slave_ind);

        test.tprintf("Stop master, then start it. If slave does not reconnect quickly, "
                     "should not get [Master]");
        test.repl->stop_node(master_ind);
        ::sleep(2);
        test.repl->start_node(master_ind);
        mxs.wait_monitor_ticks(2);
        auto status = mxs.get_servers();
        check_io_connecting(status.get(slave_ind));
        status.check_servers_status({slave_st, slave_st});

        alter_monitor(master_cond, "connecting_slave");
        test.tprintf("Should get [Master] even when slave is not yet connected.");
        mxs.wait_monitor_ticks(2);
        status = mxs.get_servers();
        check_io_connecting(status.get(slave_ind));
        status.check_servers_status({master_st, slave_st});
        mxs.check_servers_status({master_st, slave_st});

        reset();
    }

    if (test.ok())
    {
        auto relay_st = ServerInfo::RELAY | ServerInfo::SLAVE | ServerInfo::RUNNING;
        auto master_slave_chain = {master_st, relay_st, relay_st, slave_st};

        auto start_node_refresh_slave = [&test](int node_ind) {
                test.repl->start_node(node_ind);
                // Ensure slave reconnects to the server which just started.
                test.repl->connect(node_ind + 1);
                test.repl->replicate_from(node_ind + 1, node_ind);
            };

        test.tprintf("Arrange a chained topology: M->S1->S2->S3");
        test.repl->connect();
        test.repl->replicate_from(2, 1);
        test.repl->replicate_from(3, 2);
        mxs.wait_monitor_ticks();
        mxs.check_servers_status(master_slave_chain);

        test.tprintf("Stop a slave connection, should lose [Slave]");
        const int slave_ind = 1;
        test.try_query(test.repl->nodes[slave_ind], "STOP SLAVE;");
        mxs.wait_monitor_ticks();
        mxs.check_servers_status(master_3running);
        test.try_query(test.repl->nodes[slave_ind], "START SLAVE;");
        mxs.wait_monitor_ticks();

        test.tprintf("Shut down a relay, should keep [Slave]");
        test.repl->stop_node(slave_ind);
        mxs.wait_monitor_ticks();
        mxs.check_servers_status({master_st, down_st, slave_st, slave_st});     // also loses relay
        start_node_refresh_slave(slave_ind);
        mxs.wait_monitor_ticks();

        test.tprintf("Shut down master, should keep [Slave]");
        test.repl->stop_node(master_ind);
        mxs.wait_monitor_ticks();
        mxs.check_servers_status(down_3slaves);
        start_node_refresh_slave(master_ind);
        mxs.wait_monitor_ticks();

        alter_monitor(slave_cond, "linked_master");
        test.tprintf("Replication chain should still be valid");
        mxs.wait_monitor_ticks();
        mxs.check_servers_status(master_slave_chain);

        test.tprintf("Shut down a relay, should lose [Slave]s");
        test.repl->stop_node(slave_ind);
        mxs.wait_monitor_ticks();
        mxs.check_servers_status({master_st, down_st, running_st, running_st});
        start_node_refresh_slave(slave_ind);
        mxs.wait_monitor_ticks();

        test.tprintf("Stop master, then start it. If slave does not reconnect quickly, "
                     "should not get any [Slave]s");
        test.repl->stop_node(master_ind);
        ::sleep(2);
        test.repl->start_node(master_ind);
        mxs.wait_monitor_ticks(2);
        auto status = mxs.get_servers();
        check_io_connecting(status.get(slave_ind));
        status.check_servers_status(master_3running);
        // Ensure slave reconnects to master.
        test.repl->connect(slave_ind);
        test.repl->replicate_from(slave_ind, master_ind);

        alter_monitor(slave_cond, "running_master");
        test.tprintf("Replication chain should still be valid");
        mxs.check_servers_status(master_slave_chain);

        test.tprintf("Shut down master, should lose [Slave]");
        test.repl->stop_node(master_ind);
        mxs.wait_monitor_ticks();
        mxs.check_servers_status(down_3running);
        start_node_refresh_slave(master_ind);

        const char set_readonly_query[] = "SET GLOBAL read_only='%s'";
        alter_monitor(slave_cond, "writable_master");
        test.tprintf("Set master to read_only, should lose [Slave]");
        test.repl->connect(master_ind);
        test.try_query(test.repl->nodes[master_ind], set_readonly_query, "ON");
        mxs.wait_monitor_ticks();
        mxs.check_servers_status(all_running);
        test.try_query(test.repl->nodes[master_ind], set_readonly_query, "OFF");
    }

    reset();
    return test.global_result;
}
