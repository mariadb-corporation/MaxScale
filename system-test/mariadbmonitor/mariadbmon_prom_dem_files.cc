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

#include <string>
#include <maxbase/format.hh>
#include <maxtest/testconnections.hh>

using std::string;

void test_main(TestConnections& test);

int main(int argc, char** argv)
{
    // Before starting MaxScale, need to write the connection initialization file on the MaxScale machine.
    TestConnections::skip_maxscale_start(true);
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    string prom_file_path_src = mxb::string_printf("%s/mariadbmonitor/sql_promotion.txt", mxt::SOURCE_DIR);
    string dem_file_path_src = mxb::string_printf("%s/mariadbmonitor/sql_demotion.txt", mxt::SOURCE_DIR);

    string prom_file_path_dest = "/tmp/sql_promotion.txt";
    string dem_file_path_dest = "/tmp/sql_demotion.txt";

    auto& mxs = *test.maxscale;
    mxs.copy_to_node(prom_file_path_src.c_str(), prom_file_path_dest.c_str());
    mxs.copy_to_node(dem_file_path_src.c_str(), dem_file_path_dest.c_str());
    mxs.start();

    struct Globals
    {
        int64_t wait_timeout {-1};
        int64_t lock_wait_timeout {-1};
        int64_t innodb_lock_wait_timeout {-1};

        bool equals(const Globals& rhs) const
        {
            return wait_timeout == rhs.wait_timeout && lock_wait_timeout == rhs.lock_wait_timeout
                   && innodb_lock_wait_timeout == rhs.innodb_lock_wait_timeout;
        }
    };

    const Globals expect_demoted = {321, 654, 987};
    const Globals expect_promoted = {123, 456, 789};

    using NameAndConn = std::pair<string, std::unique_ptr<mxt::MariaDB>>;

    auto read_globals = [&test](NameAndConn& stuff) {
        Globals rval;
        auto res = stuff.second->query("select @@global.wait_timeout, @@global.lock_wait_timeout,"
                                       "@@global.innodb_lock_wait_timeout;");
        if (res && res->next_row())
        {
            rval.wait_timeout = res->get_int(0);
            rval.lock_wait_timeout = res->get_int(1);
            rval.innodb_lock_wait_timeout = res->get_int(2);
        }
        test.tprintf("%s global variables: wait_timeout=%li, lock_wait_timeout=%li, "
                     "innodb_lock_wait_timeout=%li", stuff.first.c_str(), rval.wait_timeout,
                     rval.lock_wait_timeout, rval.innodb_lock_wait_timeout);
        return rval;
    };

    auto write_globals = [](mxt::MariaDB* conn, const Globals& values) {
        const char fmt[] = "SET GLOBAL %s=%li;";
        conn->cmd_f(fmt, "wait_timeout", values.wait_timeout);
        conn->cmd_f(fmt, "lock_wait_timeout", values.lock_wait_timeout);
        conn->cmd_f(fmt, "innodb_lock_wait_timeout", values.innodb_lock_wait_timeout);
    };

    if (test.ok())
    {
        auto& repl = *test.repl;
        std::vector<NameAndConn> server_conns;
        server_conns.reserve(repl.N);
        for (int i = 0; i < repl.N; i++)
        {
            auto* be = repl.backend(i);
            server_conns.emplace_back(be->cnf_name(), be->open_connection());
        }

        std::vector<Globals> old_values;
        old_values.reserve(server_conns.size());
        for (auto& server_conn : server_conns)
        {
            auto globals = read_globals(server_conn);
            old_values.push_back(globals);
        }

        if (test.ok())
        {
            mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

            auto master = mxt::ServerInfo::master_st;
            auto slave = mxt::ServerInfo::slave_st;

            // Do a switchover. Check that the new master and old master have globals as set in the
            // promotion and demotions files.
            int demoted_ind = 0;
            int promoted_ind = 1;
            auto* promote_srv = repl.backend(promoted_ind);
            mxs.maxctrl("call command mariadbmon switchover MariaDB-Monitor " + promote_srv->cnf_name());
            mxs.wait_for_monitor(3);
            mxs.check_print_servers_status({slave, master, slave, slave});

            auto demoted_globals = read_globals(server_conns[demoted_ind]);
            test.expect(demoted_globals.equals(expect_demoted),
                        "Demotion didn't set expected global values");

            auto promoted_globals = read_globals(server_conns[promoted_ind]);
            test.expect(promoted_globals.equals(expect_promoted),
                        "Promotion didn't set expected global values");

            test.tprintf("Restoring old globals");
            for (size_t i = 0; i < server_conns.size(); i++)
            {
                write_globals(server_conns[i].second.get(), old_values[i]);
            }
        }
    }

    mxs.stop();
    mxs.ssh_output("rm -f " + prom_file_path_dest);
    mxs.ssh_output("rm -f " + dem_file_path_dest);
}
