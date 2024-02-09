/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <string>
#include <maxbase/format.hh>
#include "mariadbmon_utils.hh"

using std::string;

namespace
{

// How long should we keep running.
const time_t TEST_DURATION = 60;

const char* CLIENT_USER = "mysqlmon_switchover_stress";
const char* CLIENT_PASSWORD = "mysqlmon_switchover_stress";

void create_client_user(TestConnections& test)
{
    auto conn = test.maxscale->open_rwsplit_connection2();
    conn->cmd_f("create or replace user '%s' identified by '%s';", CLIENT_USER, CLIENT_PASSWORD);
    conn->cmd_f("grant select, insert, update on test.* to '%s';", CLIENT_USER);
}

void drop_client_user(TestConnections& test)
{
    auto conn = test.maxscale->open_rwsplit_connection2();
    conn->cmd_f("drop user '%s';", CLIENT_USER);
}

void switchover(TestConnections& test, int next_master_id, int current_master_id)
{
    auto& mxs = *test.maxscale;
    string next_master_name = "server" + std::to_string(next_master_id);
    string command = mxb::string_printf("call command mysqlmon switchover MySQL-Monitor %s server%i",
                                        next_master_name.c_str(), current_master_id);
    test.tprintf("Running on MaxCtrl: %s", command.c_str());
    auto res = mxs.maxctrl(command);
    if (res.rc == 0)
    {
        mxs.wait_for_monitor();

        // Check that server statuses are as expected.
        string master_name;
        int n_master = 0;

        auto servers = mxs.get_servers();
        servers.print();

        for (int i = 0; i < 4; i++)
        {
            const auto& srv = servers.get(i);
            auto status = srv.status;
            if (status == mxt::ServerInfo::master_st)
            {
                n_master++;
                test.expect(srv.name == next_master_name, "Wrong master. Got %s, expected %s.",
                            srv.name.c_str(), next_master_name.c_str());
            }
            else if (status != mxt::ServerInfo::slave_st)
            {
                test.add_failure("%s is neither master or slave. Status: %s", srv.name.c_str(),
                                 srv.status_to_string().c_str());
            }
        }

        test.expect(n_master == 1, "Expected one master, found %i.", n_master);
    }
    else
    {
        test.add_failure("Manual switchover failed: %s", res.output.c_str());
    }
}

void run(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    create_client_user(test);
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    testclient::Settings sett;
    sett.host = mxs.ip4();
    sett.port = mxs.rwsplit_port;
    sett.user = CLIENT_USER;
    sett.pw = CLIENT_PASSWORD;
    sett.rows = 20;
    testclient::ClientGroup clients(test, 4, sett);
    clients.prepare();

    if (test.ok())
    {
        clients.start();

        time_t start = time(NULL);
        int current_master_id = 1;
        int n_switchovers = 0;

        while (test.ok() && (time(NULL) - start < TEST_DURATION))
        {
            int next_master_id = current_master_id % 4 + 1;
            switchover(test, next_master_id, current_master_id);

            if (test.ok())
            {
                current_master_id = next_master_id;
                n_switchovers++;
                sleep(1);
            }
        }

        test.tprintf("Stopping clients after %i switchovers.", n_switchovers);

        clients.stop();

        // Ensure master is at server1. Shortens startup time for next test.
        if (current_master_id != 1)
        {
            switchover(test, 1, current_master_id);
        }

        mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
        drop_client_user(test);
    }

    clients.print_stats();
    clients.cleanup();
}
}

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, run);
}
