/**
 * Minimal MaxCtrl sanity check
 */

#include <maxtest/testconnections.hh>

void test_reload_tls(TestConnections& test)
{
    test.maxscale->ssh_node_f(true, "rm /var/lib/maxscale/maxscale.cnf.d/*");
    const char* home = test.maxscale->access_homedir();
    int rc = test.maxscale->ssh_node_f(true, "sed -i "
                                             " -e '/maxscale/ a admin_ssl_key=%s/certs/server-key.pem'"
                                             " -e '/maxscale/ a admin_ssl_cert=%s/certs/server-cert.pem'"
                                             " /etc/maxscale.cnf", home, home);
    test.expect(rc == 0, "Failed to enable encryption for the REST API");
    test.maxscale->restart();

    test.tprintf("TLS reload sanity check");
    test.expect(test.maxctrl("-s -n false list servers").rc == 0, "`list servers` should work");
    test.expect(test.maxctrl("list servers").rc != 0, "Command without --secure should fail");

    test.expect(test.maxctrl("-s -n false reload tls").rc == 0, "`reload tls` should work");
    test.expect(test.maxctrl("-s -n false list servers").rc == 0, "`list servers` should work");

    if (test.ok())
    {
        test.tprintf("TLS reload stress test");
        std::vector<std::thread> threads;
        std::atomic<bool> running {true};

        for (int i = 0; i < 10; i++)
        {
            threads.emplace_back([&](){
                int num = 0;
                while (running)
                {
                    ++num;
                    auto res = test.maxscale->ssh_output("maxctrl -s -n false list servers", false);
                    test.expect(res.rc == 0, "`list servers` should not fail: %d, %s", res.rc,
                                res.output.c_str());
                }

                test.tprintf("Executed %d commands", num);
            });
        }

        for (int i = 0; i < 20; i++)
        {
            auto res = test.maxctrl("-s -n false reload tls");
            test.expect(res.rc == 0, "`reload tls` should work: %d, %s", res.rc, res.output.c_str());
        }

        running = false;

        for (auto& t : threads)
        {
            t.join();
        }
    }

    test.maxscale->ssh_node_f(true, "sed -i  -e '/admin_ssl/ d' /etc/maxscale.cnf");
    test.maxscale->restart();
}

void test_cert_chain(TestConnections& test)
{
    test.maxscale->ssh_node_f(true, "rm /var/lib/maxscale/maxscale.cnf.d/*");
    const char* home = test.maxscale->access_homedir();

    int rc = test.maxscale->ssh_node_f(
        true, "cat %s/certs/server-cert.pem %s/certs/ca.pem > %s/certs/server-chain-cert.pem",
        home, home, home);
    test.expect(rc == 0, "Failed to combine certificates into a chain");

    rc = test.maxscale->ssh_node_f(
        true,
        "sed -i "
        " -e '/maxscale/ a admin_ssl_key=%s/certs/server-key.pem'"
        " -e '/maxscale/ a admin_ssl_cert=%s/certs/server-chain-cert.pem'"
        " /etc/maxscale.cnf", home, home);
    test.expect(rc == 0, "Failed to enable encryption for the REST API");
    test.maxscale->restart();

    test.expect(test.maxctrl("-s -n false list servers").rc == 0, "`list servers` should work");
    test.expect(test.maxctrl("list servers").rc != 0, "Command without --secure should fail");
    test.expect(test.maxctrl("-s -n false reload tls").rc == 0, "`reload tls` should work");
    test.expect(test.maxctrl("-s -n false list servers").rc == 0, "`list servers` should work after reload");

    test.maxscale->ssh_node_f(true, "sed -i  -e '/admin_ssl/ d' /etc/maxscale.cnf");
    test.maxscale->restart();
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    int rc = test.maxscale->ssh_node_f(false, "maxctrl --help list servers");
    test.expect(rc == 0, "`--help list servers` should work");

    rc = test.maxscale->ssh_node_f(false, "maxctrl --tsv list servers|grep 'Master, Running'");
    test.expect(rc == 0, "`list servers` should return at least one row with: Master, Running");

    rc = test.maxscale->ssh_node_f(false, "maxctrl set server server2 maintenance");
    test.expect(rc == 0, "`set server` should work");

    rc = test.maxscale->ssh_node_f(false, "maxctrl --tsv list servers|grep 'Maintenance'");
    test.expect(rc == 0, "`list servers` should return at least one row with: Maintenance");

    rc = test.maxscale->ssh_node_f(false, "maxctrl clear server server2 maintenance");
    test.expect(rc == 0, "`clear server` should work");

    rc = test.maxscale->ssh_node_f(false, "maxctrl --tsv list servers|grep 'Maintenance'");
    test.expect(rc != 0, "`list servers` should have no rows with: Maintenance");

    test.tprintf("Execute all available commands");
    test.maxscale->ssh_node_f(false,
                              "maxctrl list servers;"
                              "maxctrl list services;"
                              "maxctrl list listeners RW-Split-Router;"
                              "maxctrl list monitors;"
                              "maxctrl list sessions;"
                              "maxctrl list filters;"
                              "maxctrl list modules;"
                              "maxctrl list threads;"
                              "maxctrl list users;"
                              "maxctrl list commands;"
                              "maxctrl show server server1;"
                              "maxctrl show servers;"
                              "maxctrl show service RW-Split-Router;"
                              "maxctrl show services;"
                              "maxctrl show monitor MySQL-Monitor;"
                              "maxctrl show monitors;"
                              "maxctrl show session 1;"
                              "maxctrl show sessions;"
                              "maxctrl show filter qla;"
                              "maxctrl show filters;"
                              "maxctrl show module readwritesplit;"
                              "maxctrl show modules;"
                              "maxctrl show maxscale;"
                              "maxctrl show thread 1;"
                              "maxctrl show threads;"
                              "maxctrl show logging;"
                              "maxctrl show commands mariadbmon;"
                              "maxctrl clear server server1 maintenance;"
                              "maxctrl enable log-priority info;"
                              "maxctrl disable log-priority info;"
                              "maxctrl create server server5 127.0.0.1 3306;"
                              "maxctrl create monitor mon1 mariadbmon user=skysql password=skysql;"
                              "maxctrl create service svc1 readwritesplit user=skysql password=skysql;"
                              "maxctrl create filter qla2 qlafilter filebase=/tmp/qla2.log;"
                              "maxctrl create listener svc1 listener1 9999;"
                              "maxctrl create user maxuser maxpwd;"
                              "maxctrl link service svc1 server5;"
                              "maxctrl link monitor mon1 server5;"
                              "maxctrl alter service-filters svc1 qla2;"
                              "maxctrl unlink service svc1 server5;"
                              "maxctrl unlink monitor mon1 server5;"
                              "maxctrl alter service-filters svc1"
                              "maxctrl destroy server server5;"
                              "maxctrl destroy listener svc1 listener1;"
                              "maxctrl destroy monitor mon1;"
                              "maxctrl destroy filter qla2;"
                              "maxctrl destroy service svc1;"
                              "maxctrl destroy user maxuser;"
                              "maxctrl stop service RW-Split-Router;"
                              "maxctrl stop monitor MySQL-Monitor;"
                              "maxctrl stop maxscale;"
                              "maxctrl start service RW-Split-Router;"
                              "maxctrl start monitor MySQL-Monitor;"
                              "maxctrl start maxscale;"
                              "maxctrl alter server server1 port 3307;"
                              "maxctrl alter server server1 port 3306;"
                              "maxctrl alter monitor MySQL-Monitor auto_failover true;"
                              "maxctrl alter service RW-Split-Router max_slave_connections=3;"
                              "maxctrl alter logging ms_timestamp true;"
                              "maxctrl alter maxscale passive true;"
                              "maxctrl rotate logs;"
                              "maxctrl call command mariadbmon reset-replication MySQL-Monitor;"
                              "maxctrl api get servers;"
                              "maxctrl classify 'select 1';");

    test.tprintf("MXS-3697: MaxCtrl fails with \"ENOENT: no such file or directory, stat '/~/.maxctrl.cnf'\" "
                 "when running commands from the root directory.");
    rc = test.maxscale->ssh_node_f(false, "cd / && maxctrl list servers");
    test.expect(rc == 0, "Failed to execute a command from the root directory");

    test.tprintf("MXS-4169: Listeners wrongly require ssl_ca_cert when created at runtime");
    test.check_maxctrl("create service my-test-service readconnroute user=maxskysql password=skysql");
    std::string home = test.maxscale->access_homedir();
    test.check_maxctrl(
        "create listener my-test-service my-test-listener 6789 ssl=true "
        "ssl_key=" + home + "/certs/server-key.pem "
        + "ssl_cert=" + home + "/certs/server-cert.pem");
    test.check_maxctrl("destroy listener my-test-listener");
    test.check_maxctrl("destroy service my-test-service");

    // Also checks that MaxCtrl works correctly when the REST API uses encryption.
    test.tprintf("MXS-4041: Reloading of REST API TLS certificates");
    test_reload_tls(test);

    test.tprintf("MXS-4442: TLS certificate chain in admin_ssl_cert");
    test_cert_chain(test);

    test.tprintf("MXS-4171: Runtime modifications to static parameters");

    auto res = test.maxctrl("alter service RW-Split-Router router=readwritesplit");
    test.expect(res.rc == 0, "Changing `router` to its current value should succeed: %s", res.output.c_str());

    res = test.maxctrl("alter service RW-Split-Router router=readconnroute");
    test.expect(res.rc != 0, "Changing `router` to a new value should fail.");

    res = test.maxctrl("alter listener RW-Split-Listener protocol=MySQLClient");
    test.expect(res.rc == 0, "Old alias for module name should compare equal: %s", res.output.c_str());

    res = test.maxctrl("alter listener RW-Split-Listener protocol=cdc");
    test.expect(res.rc != 0, "Changing listener protocol should fail.");

    test.check_maxscale_alive();
    return test.global_result;
}
