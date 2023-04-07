/**
 * @file bug448.cpp bug448 regression case ("Wildcard in host column of mysql.user table don't work properly")
 *
 * Test creates user1@xxx.%.%.% and tries to use it to connect
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <maxbase/format.hh>
#include <maxtest/testconnections.hh>

using std::string;

namespace
{
void   test_main(TestConnections& test);
string get_my_ip(TestConnections& test, const string& remote_ip);
}

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

namespace
{
void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;

    string my_ip = get_my_ip(test, mxs.ip4());
    if (!my_ip.empty())
    {
        test.tprintf("Test machine IP (got via network request): %s", my_ip.c_str());
        auto dot_loc = my_ip.find('.');
        if (dot_loc != string::npos)
        {
            string ip_wildcarded = my_ip.substr(0, dot_loc);
            ip_wildcarded.append(".%.%.%");
            test.tprintf("Test machine IP after wildcarding: %s", ip_wildcarded.c_str());

            const char un[] = "user1";
            const char pw[] = "pass1";

            string userhost = mxb::string_printf("'%s'@'%s'", un, ip_wildcarded.c_str());
            auto userhostc = userhost.c_str();

            auto test_login = [&](const string& db, bool expect_success) {
                bool mxs_login = false;
                bool query_ok = false;
                auto conn = mxs.try_open_rwsplit_connection(un, pw, db);
                if (conn->is_open())
                {
                    mxs_login = true;
                    auto res = conn->try_query("select 123;");
                    if (res && res->next_row())
                    {
                        query_ok = true;
                    }
                }

                if (expect_success)
                {
                    if (query_ok)
                    {
                        test.tprintf("%s logged into db %s, as expected.", un, db.c_str());
                    }
                    else if (mxs_login)
                    {
                        test.add_failure("%s logged into db %s on MaxScale yet query failed.",
                                         un, db.c_str());
                    }
                    else
                    {
                        test.add_failure("%s failed to log in to db %s", un, db.c_str());
                    }
                }
                else
                {
                    // If expecting failure, then even managing to log in to MaxScale is too much.
                    if (!mxs_login)
                    {
                        test.tprintf("%s failed to log into db %s, as expected.", un, db.c_str());
                    }
                    else
                    {
                        test.add_failure("%s logged into db %s, when failure was expected.",
                                         un, db.c_str());
                    }
                }
            };

            test.tprintf("Creating user %s", userhostc);
            auto admin_conn = mxs.open_rwsplit_connection2_nodb();
            auto user1 = admin_conn->create_user(un, ip_wildcarded, pw);
            const char dbname[] = "test";
            user1.grant_f("select on %s.*", dbname);

            auto reload_users = [&mxs]() {
                // Try to log in with a nonexistent user.
                sleep(1);
                mxs.try_open_rwsplit_connection("nevergonna", "giveyouup");
            };

            if (test.ok())
            {
                test.tprintf("Trying to log in as %s to db '%s'.", un, dbname);
                test_login(dbname, true);

                admin_conn->cmd_f("REVOKE select on %s.* FROM %s;", dbname, userhostc);
                // Refresh privs on MaxScale, then try to log in as user1 again. It should fail.
                reload_users();
                test_login(dbname, false);
            }

            // MXS-3172 Test logging on to database when grant includes a wildcard.
            if (test.ok())
            {
                const char grant_db[] = "Area5_Files";
                const char fail_db1[] = "Area51Files";
                const char fail_db2[] = "Area52Files";

                const char create_db_fmt[] = "create database %s;";
                admin_conn->cmd_f(create_db_fmt, grant_db);
                admin_conn->cmd_f(create_db_fmt, fail_db1);
                admin_conn->cmd_f(create_db_fmt, fail_db2);

                const char grant_fmt[] = "GRANT SELECT ON `%s`.* TO %s;";
                const char revoke_fmt[] = "REVOKE SELECT ON `%s`.* FROM %s;";
                const char escaped_wc_db[] = "Area5\\_Files";
                admin_conn->cmd_f(grant_fmt, escaped_wc_db, userhostc);
                reload_users();

                if (test.ok())
                {
                    test.tprintf("Testing database grant with escaped wildcard...");
                    test_login(grant_db, true);
                    test_login(fail_db1, false);
                    test_login(fail_db2, false);
                }

                // Replace escaped wc grant with non-escaped version.
                admin_conn->cmd_f(revoke_fmt, escaped_wc_db, userhostc);
                admin_conn->cmd_f(grant_fmt, "Area5_Files", userhostc);
                reload_users();

                if (test.ok())
                {
                    test.tprintf("Testing database grant with wildcard...");
                    test_login(grant_db, true);
                    test_login(fail_db1, true);
                    test_login(fail_db2, true);
                }

                const char drop_db_fmt[] = "drop database %s;";
                admin_conn->cmd_f(drop_db_fmt, grant_db);
                admin_conn->cmd_f(drop_db_fmt, fail_db1);
                admin_conn->cmd_f(drop_db_fmt, fail_db2);
            }
        }
        else
        {
            test.add_failure("%s is not a valid ip.", my_ip.c_str());
        }
    }
    else
    {
        test.add_failure("get_my_ip() failed.");
    }
}

string get_my_ip(TestConnections& test, const string& remote_ip)
{
    string rval;
    int sock = socket (AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0)
    {
        int dns_port = 53;
        sockaddr_in serv {};
        serv.sin_family = AF_INET;
        serv.sin_addr.s_addr = inet_addr(remote_ip.c_str());
        serv.sin_port = htons(dns_port);
        connect(sock, (const sockaddr*) &serv, sizeof(serv));

        sockaddr_in name;
        socklen_t namelen = sizeof(name);
        getsockname(sock, (sockaddr*)&name, &namelen);
        close(sock);

        char buffer[100];
        const char* p = inet_ntop(AF_INET, &name.sin_addr, buffer, 100);
        if (p)
        {
            rval = buffer;
        }
        else
        {
            test.tprintf("inet_ntop failed. Error %d: %s", errno, strerror(errno));
        }
    }
    return rval;
}
}
