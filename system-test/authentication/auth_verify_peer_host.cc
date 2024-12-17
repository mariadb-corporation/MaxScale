/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/string.hh>

using std::string;
using mxb::Json;

void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;

    auto get_sessions = [&]() {
        auto res = mxs.maxctrlf("api get sessions data[].attributes.connections");
        Json sessions;
        test.expect(sessions.load_string(res.output), "Failed to get sessions from MaxScale");
        return sessions.get_array_elems();
    };

    auto get_session_connections = [&test](const Json& session) {
        auto conns = session.get_array_elems();
        std::vector<string> server_names;
        for (auto& conn : conns)
        {
            server_names.push_back(conn.get_string("server"));
        }
        auto list = mxb::create_list_string(server_names, ",");
        test.tprintf("Session is connected to {%s}.", list.c_str());
        return server_names;
    };

    /**
     * Inject fixed hostnames into the MaxScale VM for the backend nodes. The actual hostnames
     * might be different depending on where the nodes are located. This way, the test can
     * be made much simpler.
     */
    for (int i = 0; i < test.repl->n_nodes(); i++)
    {
        test.maxscale->ssh_node_f(true, "echo '%s node00%d' >> /etc/hosts", test.repl->ip(i), i);
    }

    test.maxscale->start();

    test.tprintf("Server peer cert & host verification is on and servers have valid certificates. "
                 "All should be working normally.");
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    test.tprintf("Start a session. Check that it connects to all backends.");
    auto conn = mxs.open_rwsplit_connection2_nodb();

    auto sessions = get_sessions();
    test.expect(sessions.size() == 1, "Expected one session, found %zu.", sessions.size());
    if (!sessions.empty())
    {
        auto conns = get_session_connections(sessions[0]);
        int exp_conns = 4;
        test.expect((int)conns.size() == exp_conns, "Expected %i backend connections, found %zu.",
                    exp_conns, conns.size());
    }

    test.tprintf("Close the session.");
    conn->close();
    sessions = get_sessions();
    test.expect(sessions.empty(), "Expected no sessions, found %zu.", sessions.size());

    if (test.ok())
    {
        test.tprintf("Use bad certificates on servers 3 & 4. These certificates are correctly signed but "
                     "do not contain the correct host. Router connections to these servers should fail.");

        const char crt_dir[] = "/etc/ssl-cert";
        const char crt_file[] = "server.crt";
        const char crt_bu[] = "server.crt.backup";
        auto change_certificate = [&](mxt::MariaDBServer* srv) {
            srv->stop_database();
            auto& node = srv->vm_node();
            auto mv_res = node.run_cmd_output_sudof("mv %s/%s %s/%s", crt_dir, crt_file, crt_dir, crt_bu);
            auto cp_res = node.run_cmd_output_sudof("cp %s/server-wrong-host.crt %s/%s",
                                                    crt_dir, crt_dir, crt_file);
            test.expect(mv_res.rc == 0 && cp_res.rc == 0, "Certificate swap failed on %s.",
                        srv->cnf_name().c_str());
            srv->start_database();
        };

        auto restore_certificate = [&](mxt::MariaDBServer* srv) {
            srv->stop_database();
            auto& node = srv->vm_node();
            auto mv_res = node.run_cmd_output_sudof("mv %s/%s %s/%s", crt_dir, crt_bu, crt_dir, crt_file);
            test.expect(mv_res.rc == 0, "Certificate restore failed on %s.", srv->cnf_name().c_str());
            srv->start_database();
        };

        change_certificate(repl.backend(2));
        change_certificate(repl.backend(3));
        mxs.wait_for_monitor();
        test.tprintf("Monitor should not connect to servers 3 & 4.");
        mxs.check_print_servers_status({mxt::ServerInfo::master_st, mxt::ServerInfo::slave_st,
                                        mxt::ServerInfo::DOWN, mxt::ServerInfo::DOWN});

        conn = mxs.open_rwsplit_connection2_nodb();
        sessions = get_sessions();
        test.expect(sessions.size() == 1, "Expected one session, found %zu.", sessions.size());
        if (!sessions.empty())
        {
            auto conns = get_session_connections(sessions[0]);
            int exp_conns = 2;
            test.expect((int)conns.size() == exp_conns, "Expected %i backend connections, found %zu.",
                        exp_conns, conns.size());
        }

        test.tprintf("Restore certificates on servers 3 & 4.");
        restore_certificate(repl.backend(2));
        restore_certificate(repl.backend(3));
        mxs.wait_for_monitor();
        mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
    }

    for (int i = 0; i < test.repl->n_nodes(); i++)
    {
        test.maxscale->ssh_node_f(true, "sed -i '/node00%d/ d' /etc/hosts", i);
    }
}

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    return TestConnections().run_test(argc, argv, test_main);
}
