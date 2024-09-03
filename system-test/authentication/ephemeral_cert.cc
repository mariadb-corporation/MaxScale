/*
 * Copyright (c) 2024 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>

using std::string;

namespace
{
void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;

    mxs.check_print_servers_status({mxt::ServerInfo::master_st});

    // Helper function which attempts ssl connection with certificate verification but does not assign a CA-
    // certificate. Requires Connector-C 3.4.
    auto test_connect_rwsplit_ssl = [&](bool verify_cert) {
        auto conn = std::make_unique<mxt::MariaDB>(test.logger());
        auto& sett = conn->connection_settings();
        sett.user = mxs.user_name();
        sett.password = mxs.password();
        sett.ssl.enabled = true;
        if (verify_cert)
        {
            sett.ssl.verify_peer = true;
            sett.ssl.verify_host = true;
        }

        string with = verify_cert ? "with" : "without";
        with.append(" peer certificate verification");

        conn->try_open(mxs.ip(), mxs.rwsplit_port, "test");
        if (conn->is_open())
        {
            auto res = conn->try_query("select 1;");
            if (res && res->next_row())
            {
                test.tprintf("Connection and query %s succeeded.", with.c_str());
            }
            else
            {
                test.add_failure("Query %s failed.", with.c_str());
            }
        }
        else
        {
            test.add_failure("Connection %s failed.", with.c_str());
        }
    };

    test_connect_rwsplit_ssl(false);
    test_connect_rwsplit_ssl(true);

    auto be_vrs = repl.backend(0)->status().version_num;
    const int vrs_required = 110401;
    if (be_vrs >= vrs_required)
    {
        mxs.stop();
        auto res1 = mxs.vm_node().run_cmd_output_sudo("sed -i 's/ssl=0/ssl=1/' /etc/maxscale.cnf");
        auto res2 = mxs.vm_node().run_cmd_output_sudo("sed -i 's/ssl_verify_peer_certificate=0/"
                                                      "ssl_verify_peer_certificate=1/' /etc/maxscale.cnf");
        auto res3 = mxs.vm_node().run_cmd_output_sudo("sed -i 's/ssl_verify_peer_host=0/"
                                                      "ssl_verify_peer_host=1/' /etc/maxscale.cnf");
        test.expect(res1.rc == 0 && res2.rc == 0 && res3.rc == 0, "MaxScale config file edit failed.");
        mxs.start_and_check_started();

        if (test.ok())
        {
            test.tprintf("Ephemeral certificate checking enabled for server1. Monitor should be able to "
                         "connect.");
            mxs.wait_for_monitor();
            mxs.check_print_servers_status({mxt::ServerInfo::master_st});
            test.tprintf("Testing routing sessions.");
            test_connect_rwsplit_ssl(false);
            test_connect_rwsplit_ssl(true);
        }
    }
    else
    {
        // TODO: this should be a test error at some point.
        test.tprintf("Skipping backend test due to old MariaDB Server version. Found %lu, need %i.",
                     be_vrs, vrs_required);
    }
}
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
