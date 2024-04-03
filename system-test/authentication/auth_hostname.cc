/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/host.hh>

namespace
{
void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;
    const char user[] = "testuser";
    const char pw1[] = "pass1";
    const char pw2[] = "pass2";
    const char pw3[] = "pass3";
    test.tprintf("Creating user '%s' with 3 different passwords for different hosts.", user);
    auto conn = mxs.open_rwsplit_connection2_nodb();
    auto user1 = conn->create_user(user, "non_existing_host1", pw1);
    auto user2 = conn->create_user(user, "%", pw2);
    auto user3 = conn->create_user(user, "non_existing_host2", pw3);

    repl.sync_slaves();

    const char unexpected_success[] = "Login with '%s' succeeded when it should have failed";
    test.tprintf("Trying first hostname, expecting failure");
    auto client_conn = mxs.try_open_rwsplit_connection(user, pw1);
    test.expect(!client_conn->is_open(), unexpected_success, pw1);

    test.tprintf("Trying second hostname, expecting success");
    client_conn = mxs.try_open_rwsplit_connection(user, pw2);
    test.expect(client_conn->is_open(), "Login with '%s' failed.", pw2);

    test.tprintf("Trying third hostname, expecting failure");
    client_conn = mxs.try_open_rwsplit_connection(user, pw3);
    test.expect(!client_conn->is_open(), unexpected_success, pw3);

    if (test.ok())
    {
        // Get hostname of test machine.
        auto res = test.run_shell_cmd_output("hostname");
        test.expect(res.rc == 0 && !res.output.empty(), "'hostname' failed or gave no results: %s",
                    res.output.c_str());
        const char* hostname = res.output.c_str();
        auto host = mxb::Host::from_string(res.output, 1);
        test.expect(host.type() == mxb::Host::Type::HostName, "'%s' is not a valid hostname.", hostname);
        if (test.ok())
        {
            const char host_user[] = "host_user";
            const char host_pw[] = "pass4";
            // The full network hostname may be different from the one read by 'hostname', so add wildcard.
            std::string wc_hostname = hostname;
            wc_hostname.push_back('%');
            test.tprintf("Creating user '%s'@'%s'.", host_user, wc_hostname.c_str());
            auto user4 = conn->create_user(host_user, wc_hostname, host_pw);

            repl.sync_slaves();

            test.tprintf("Logging in as '%s'.", host_user);
            client_conn = mxs.try_open_rwsplit_connection(host_user, host_pw);
            test.expect(client_conn->is_open(), "Login with '%s' failed.", host_user);

            if (test.ok())
            {
                // Finally, test that multiple clients can log in concurrently.
                mxb::StopWatch timer;
                std::vector<std::thread> threads;
                for (int i = 0; i < 100; i++)
                {
                    auto thread_func = [&test, &mxs, host_user, host_pw, i]() {
                        auto rw_conn = mxs.try_open_rwsplit_connection(host_user, host_pw);
                        test.expect(rw_conn->is_open(), "Client conn %i failed.", i);
                    };
                    threads.push_back(std::thread(std::move(thread_func)));
                }
                for (auto& thd : threads)
                {
                    thd.join();
                }
                test.tprintf("Testing 100 clients took %.2f seconds.", mxb::to_secs(timer.lap()));
            }
        }
    }
}
}

int main(int argc, char* argv[])
{
    return TestConnections().run_test(argc, argv, test_main);
}
