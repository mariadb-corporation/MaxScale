/*
 * Copyright (c) 2024 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/format.hh>

using std::string;

namespace
{
void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;

    enum class Type {READWRITE, READONLY};
    auto test_cmd = [&](Type type, bool expect_success) {
        string cmd = (type == Type::READWRITE) ? "call command mariadbmon switchover MariaDB-Monitor" :
            "list servers";
        auto res = mxs.maxctrl(cmd);
        bool ok = res.rc == 0;
        const char* type_str = (type == Type::READWRITE) ? "Read-write" : "Read-only";
        const char* res_str = ok ? "succeeded" : "failed";
        if (ok == expect_success)
        {
            test.tprintf("%s command %s as expected.", type_str, res_str);
        }
        else
        {
            test.add_failure("%s command %s when the opposite was expected.", type_str, res_str);
        }
    };

    auto alter_setting = [&test, &mxs](const char* setting, const char* new_val) {
        mxs.stop_and_check_stopped();
        test.tprintf("Setting '%s' to '%s'.", setting, new_val);
        string sed_cmd = mxb::string_printf(
            "sed -i \"s|%s=.*|%s=%s|\" /etc/maxscale.cnf", setting, setting, new_val);
        mxs.ssh_output(sed_cmd);
        mxs.start_and_check_started();
    };
    auto alter_rw = [&alter_setting](const char* new_val) {
        alter_setting("admin_readwrite_hosts", new_val);
    };
    auto alter_ro = [&alter_setting](const char* new_val) {
        alter_setting("admin_readonly_hosts", new_val);
    };
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    if (test.ok())
    {
        // All rest-api connections originate from 127.0.0.1/localhost.
        // With default settings, both RO and RW rest commands should be allowed.
        test.tprintf("Admin commands should be allowed from all ip:s (%%).");
        test_cmd(Type::READWRITE, true);
        test_cmd(Type::READONLY, true);

        test.tprintf("Blocking read-write commands.");
        alter_rw("aabbcc");
        test_cmd(Type::READWRITE, false);

        test.tprintf("Blocking read-only commands.");
        alter_ro("127.0.0.2");

        test.tprintf("Testing CIDR-notation.");
        alter_ro("127.0.0.2\\/16");
        test_cmd(Type::READONLY, true);

        test.tprintf("Test with a list of values. Localhost does not match any ip as it matches unix pipe.");
        alter_rw("128.0.0.1,localhost");
        test_cmd(Type::READWRITE, false);
        alter_ro("::ffff:127.0.0.1/128,aabbcc");
        test_cmd(Type::READONLY, true);

        test.tprintf("Test with a wildcard hostname.");
        alter_rw("localhos%,aabbcc.com");
        test_cmd(Type::READWRITE, true);
        alter_ro("aabbcc,localh_ost");
        test_cmd(Type::READONLY, true);
    }

    mxs.maxctrl("call command mariadbmon switchover MariaDB-Monitor server1");
    mxs.wait_for_monitor();
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
}
}
int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
