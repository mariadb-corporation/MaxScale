#include <maxtest/testconnections.hh>
#include <maxbase/assert.h>

#include "config_sync_common.hh"

using namespace std::chrono;
using JsonType = mxb::Json::JsonType;
using mxt::MaxScale;

const auto NORMAL = mxb::Json::Format::NORMAL;

RestApi api1;
RestApi api2;

struct TestCase
{
    std::string desc;       // Test description
    std::string cmd;        // The MaxCtrl command to execute
    std::string endpoint;   // REST API endpoint to check, optional
    std::string ptr;        // JSON Pointer to field to check, optional

    void execute(TestConnections& test, MaxScale* maxscale) const
    {
        test.tprintf("  %s", desc.c_str());
        auto res = maxscale->maxctrl(cmd);
        test.expect(res.rc == 0, "MaxCtrl command '%s' failed: %s", cmd.c_str(), res.output.c_str());
    }
};

std::vector<TestCase> tests
{
    {
        "Change router parameter",
        "alter service RW-Split-Router max_sescmd_history 5",
        "services/RW-Split-Router",
        "/data/attributes/parameters/max_sescmd_history"
    },
    {
        "Change router parameter on the second MaxScale",
        "alter service RW-Split-Router max_sescmd_history 15",
        "services/RW-Split-Router",
        "/data/attributes/parameters/max_sescmd_history"
    },
    {
        "Create server",
        "create server test-server 127.0.0.1 3306",
        "servers/test-server",
        "/data/attributes/parameters"
    },
    {
        "Alter server",
        "alter server test-server port 3333",
        "servers/test-server",
        "/data/attributes/parameters/port"
    },
    {
        "Link server to monitor",
        "link monitor MariaDB-Monitor test-server",
        "monitors/MariaDB-Monitor",
        "/data/relationships/servers/data"
    },
    {
        "Unlink server from monitor",
        "unlink monitor MariaDB-Monitor test-server",
        "monitors/MariaDB-Monitor",
        "/data/relationships/servers/data"
    },
    {
        "Link server to service",
        "link service RW-Split-Router test-server",
        "services/RW-Split-Router",
        "/data/relationships/servers/data"
    },
    {
        "Unlink server from service",
        "unlink service RW-Split-Router test-server",
        "services/RW-Split-Router",
        "/data/relationships/servers/data"
    },
    {
        "Destroy server",
        "destroy server test-server",
    },
    {
        "Create service",
        "create service test-service readconnroute user=maxskysql password=skysql router_options=master",
        "services/test-service",
        "/data/attributes/parameters"
    },
    {
        "Alter service",
        "alter service test-service router_options slave",
        "services/test-service",
        "/data/attributes/parameters"
    },
    {
        "Destroy service",
        "destroy service test-service"
    },
    {
        "Create filter",
        "create filter test-filter qlafilter filebase=/tmp/qla-log log_type=unified append=true",
        "filters/test-filter",
        "/data/attributes/parameters"
    },
    {
        "Destroy filter",
        "destroy filter test-filter"
    },
    {
        "Create listener",
        "create listener RW-Split-Router test-listener 3306",
        "listeners/test-listener",
        "/data/attributes/parameters"
    },
    {
        "Destroy listener",
        "destroy listener RW-Split-Router test-listener"
    },
    {
        "Create monitor",
        "create monitor test-monitor galeramon user=maxskysql password=skysql",
        "monitors/test-monitor",
        "/data/attributes/parameters"
    },
    {
        "Create service that uses the monitor",
        "create service test-service2 readconnroute user=maxskysql password=skysql router_options=master --cluster test-monitor",
        "services/test-service2",
        "/data/attributes/parameters"
    },
    {
        "Destroy monitor",
        "destroy monitor --force test-monitor"
    },
    {
        "Destroy service that uses the monitor",
        "destroy service --force test-service2"
    },
};

void wait_for_sync(int version = 0)
{
    auto start = steady_clock::now();

    while (steady_clock::now() - start < seconds(5))
    {
        auto res1 = get(api1, "maxscale", "/data/attributes/config_sync");
        auto res2 = get(api2, "maxscale", "/data/attributes/config_sync");
        int v1 = res1.get_int("version");
        int v2 = res2.get_int("version");

        if (v1 == v2 && (version == 0 || v1 == version)
            && res1.get_object("nodes").keys().size() == 2
            && res2.get_object("nodes").keys().size() == 2)
        {
            break;
        }
        else
        {
            std::this_thread::sleep_for(milliseconds(100));
        }
    }
}

void create_config(MaxScale* mxs, const std::string& config)
{
    mxs->stop();
    mxs->ssh_node_f(true,
                    "echo '%s' > /var/lib/maxscale/maxscale-config.json;"
                    "chown maxscale:maxscale /var/lib/maxscale/maxscale-config.json;",
                    config.c_str());
    mxs->start();

    // This is a bit crude but it's needed in case maxscale ends up restarting
    mxs->ssh_node("for ((i=0;i<10;i++)); do maxctrl show maxscale && break; done", true);
}

std::string get_diff(const mxb::Json& js_a, const mxb::Json& js_b)
{
    if (!js_a.valid() || !js_b.valid() || js_a == js_b)
    {
        return "";
    }

    std::string a = js_a.to_string(NORMAL);
    std::string b = js_b.to_string(NORMAL);
    auto start = std::mismatch(a.begin(), a.end(), b.begin(), b.end());
    auto end = std::mismatch(a.rbegin(), a.rend(), b.rbegin(), b.rend());

    mxb_assert(start.first != a.end() && end.first != a.rend());

    while (start.first != a.begin() && start.second != b.begin())
    {
        char c = *start.first;

        if (c == ',' || c == '[' || c == '{')
        {
            break;
        }

        --start.first;
        --start.second;
    }

    // Skip over the delimiting character
    ++start.first;
    ++start.second;

    while (end.first != a.rbegin() && end.second != b.rbegin())
    {
        char c = *end.first;

        if (c == ',' || c == '[' || c == '{')
        {
            break;
        }

        --end.first;
        --end.second;
    }

    std::string a_diff(start.first, end.first.base());
    std::string b_diff(start.second, end.second.base());

    return a_diff + " != " + b_diff;
}

void expect_sync(TestConnections& test, int expected_version, size_t num_maxscales)
{
    bool ok = true;
    std::ostringstream ss;

    auto check = [&](auto status, const char* who) {
            int version = status.get_int("version");
            test.expect(version == expected_version,
                        "Expected version %d, got %d from %s", expected_version, version, who);

            auto nodes = status.get_object("nodes");
            size_t num_fields = json_object_size(nodes.get_json());

            test.expect(num_fields == num_maxscales,
                        "Expected \"nodes\" object to have %lu fields, got %lu from %s: %s",
                        num_maxscales, num_fields, who, nodes.to_string(NORMAL).c_str());

            test.expect(status.contains("origin"), "Expected \"origin\" to not be empty.");
            test.expect(status.contains("status"), "Expected \"status\" to not be empty.");
        };

    wait_for_sync();

    auto status1 = get(api1, "maxscale", "/data/attributes/config_sync");
    auto status2 = get(api2, "maxscale", "/data/attributes/config_sync");

    check(status1, "MaxScale 1");
    check(status2, "MaxScale 2");

    if (ok)
    {
        test.expect(status1 == status2, "Expected JSON to be equal: %s",
                    get_diff(status1, status2).c_str());
    }

    test.expect(ok, "%s", ss.str().c_str());
}

void expect_equal(TestConnections& test, const std::string& resource, const std::string& path)
{
    if (resource.empty())
    {
        return;
    }

    auto value1 = get(api1, resource, path);
    auto value2 = get(api2, resource, path);

    test.expect(value1 == value2, "Values for '%s' at '%s' are not equal: %s",
                resource.c_str(), path.c_str(), get_diff(value1, value2).c_str());
}

void reset(TestConnections& test)
{
    test.stop_all_maxscales();

    test.maxscale->ssh_output("rm -r /var/lib/maxscale/*");
    test.maxscale2->ssh_output("rm -r /var/lib/maxscale/*");

    auto conn = test.repl->get_connection(0);
    test.expect(conn.connect(), "Connection failed: %s", conn.error());
    conn.query("DROP TABLE mysql.maxscale_config");

    test.maxscale->start();
    test.maxscale2->start();
}

void test_config_parameters(TestConnections& test)
{
    for (auto cmd : {
        "alter maxscale config_sync_cluster some-monitor",
        "destroy monitor --force MariaDB-Monitor"
    })
    {
        test.expect(test.maxscale->maxctrl(cmd).rc != 0,
                    "Command should fail: %s", cmd);
    }

    test.tprintf("Disabling and then enabling config_sync_cluster should not increment version");
    auto res = test.maxscale->maxctrl("alter maxscale config_sync_cluster \"\"");
    test.expect(res.rc == 0, "Disabling config_sync_cluster failed: %s", res.output.c_str());

    res = test.maxscale->maxctrl("alter maxscale config_sync_cluster MariaDB-Monitor");
    test.expect(res.rc == 0, "Enabling config_sync_cluster failed: %s", res.output.c_str());

    auto sync = get(api1, "maxscale", "/data/attributes/config_sync");
    test.expect(sync.type() == JsonType::JSON_NULL,
                "\"config_sync\" should be null after toggling config_sync_cluster: %s",
                sync.to_string(NORMAL).c_str());

    res = test.maxscale->maxctrl("alter maxscale config_sync_cluster \"\"");
    test.expect(res.rc == 0, "Disabling config_sync_cluster failed: %s", res.output.c_str());

    res = test.maxscale->maxctrl("alter service RW-Split-Router max_sescmd_history 123");
    test.expect(res.rc == 0, "Config change without config_sync_cluster failed: %s", res.output.c_str());

    res = test.maxscale2->maxctrl("alter service RW-Split-Router max_sescmd_history 321");
    test.expect(res.rc == 0, "Config change on second MaxScale should work: %s", res.output.c_str());

    sync = get(api1, "maxscale", "/data/attributes/config_sync");
    test.expect(sync.type() == JsonType::JSON_NULL,
                "\"config_sync\" should be null after modification in the cluster: %s",
                sync.to_string(NORMAL).c_str());

    res = test.maxscale->maxctrl("alter maxscale config_sync_cluster MariaDB-Monitor");
    test.expect(res.rc == 0, "Enabling config_sync_cluster failed: %s", res.output.c_str());

    expect_sync(test, 1, 2);
    expect_equal(test, "services/RW-Split-Router", "/data/attributes/parameters");

    res = test.maxscale->maxctrl("alter service RW-Split-Router max_sescmd_history 123");
    test.expect(res.rc == 0, "Config change failed after enabling config_sync_cluster: %s",
                res.output.c_str());

    auto version0 = get_version(api1);
    res = test.maxscale->maxctrl("alter service RW-Split-Router max_sescmd_history 123");
    test.expect(res.rc == 0, "First no-op change failed: %s", res.output.c_str());

    auto version1 = get_version(api1);
    test.expect(version0 == version1, "First no-op change should not increment version: %ld != %ld",
                version0, version1);

    res = test.maxscale->maxctrl("alter service RW-Split-Router max_sescmd_history 123");
    test.expect(res.rc == 0, "Second no-op change failed: %s", res.output.c_str());

    auto version2 = get_version(api1);
    test.expect(version0 == version2, "Second no-op change should not increment version: %ld != %ld",
                version0, version2);

    res = test.maxscale->maxctrl("alter maxscale config_sync_user bob");
    test.expect(res.rc == 0, "Changing config_sync_user to a bad user failed: %s", res.output.c_str());
    test.expect(version0 == get_version(api1), "Changing config_sync_user should not increment version");

    res = test.maxscale->maxctrl("alter service RW-Split-Router max_sescmd_history 124");
    test.expect(res.rc != 0, "Config change with bad credentials should fail");
    test.expect(version0 == get_version(api1),
                "Config update with bad credentials should not increment version");

    res = test.maxscale->maxctrl("alter maxscale --skip-sync config_sync_user maxskysql");
    test.expect(res.rc == 0, "Changing config_sync_user back failed: %s", res.output.c_str());
    test.expect(version0 == get_version(api1), "Changing config_sync_user should not increment version");

    res = test.maxscale->maxctrl("alter service RW-Split-Router max_sescmd_history 124");
    test.expect(res.rc == 0, "Config change with good credentials should work");
    expect_sync(test, version0 + 1, 2);

    reset(test);
}

void test_sync(TestConnections& test)
{
    // Each test case should increment the version by one
    int version = 1;

    test.tprintf("Execute tests with both MaxScales running");

    for (const auto& t : tests)
    {
        t.execute(test, test.maxscale);
        expect_sync(test, version++, 2);
        expect_equal(test, t.endpoint, t.ptr);
    }

    test.tprintf("Execute tests with only one MaxScale");
    test.maxscale2->stop();

    std::string commands;

    for (const auto& t : tests)
    {
        commands += "'" + t.cmd + "' ";
    }

    auto res = test.maxscale->ssh_node_f(
        false, "for cmd in %s; do echo $cmd; done|maxctrl", commands.c_str());
    test.expect(res == 0, "MaxCtrl commands failed");

    test.tprintf("Start the second MaxScale and make sure it catches up");

    version = get_version(api1);
    test.maxscale2->start();
    expect_sync(test, version, 2);

    test.tprintf("Sync new monitor with service relationship");
    test.maxscale2->stop();
    test.maxscale->maxctrl("create monitor test-monitor galeramon user=maxskysql password=skysql");
    test.maxscale->maxctrl("create service test-service2 readconnroute "
                           "user=maxskysql password=skysql router_options=master --cluster test-monitor");
    test.maxscale2->start();

    version += 2;
    expect_sync(test, version, 2);

    test.maxscale->maxctrl("destroy monitor --force test-monitor");
    test.maxscale->maxctrl("destroy service --force test-service2");

    version += 2;
    expect_sync(test, version, 2);

    reset(test);
}

void test_bad_change(TestConnections& test)
{
    test.tprintf("Do a configuration change that is expected to work");
    test.maxscale->maxctrl("alter service RW-Split-Router max_sescmd_history 15");
    expect_sync(test, 1, 2);
    expect_equal(test, "services/RW-Split-Router", "/data/attributes/parameters");

    test.tprintf("Create a filter that only works on one MaxScale");
    const char REMOVE_DIR[] = "rm -rf /tmp/path-that-exists-on-mxs1/";
    const char CREATE_DIR[] = "mkdir --mode 0777 -p /tmp/path-that-exists-on-mxs1/";
    test.maxscale->ssh_node(CREATE_DIR, false);

    // Make sure the path on the other Maxscale doesn't exist
    test.maxscale2->ssh_node(REMOVE_DIR, false);

    auto res = test.maxscale->maxctrl("create filter test-filter qlafilter "
                                      "log_type=unified append=true "
                                      "filebase=/tmp/path-that-exists-on-mxs1/qla.log");
    test.expect(res.rc == 0, "Creating the filter should work");

    wait_for_sync();

    auto sync1 = get(api1, "maxscale", "/data/attributes/config_sync");
    auto sync2 = get(api2, "maxscale", "/data/attributes/config_sync");
    int64_t version1 = sync1.get_int("version");
    int64_t version2 = sync2.get_int("version");

    test.expect(version1 == version2,
                "Second MaxScale should be at version %ld but it is at %ld",
                version1, version2);

    std::string cksum1 = sync1.get_string("checksum");
    std::string cksum2 = sync2.get_string("checksum");

    test.expect(cksum1 != cksum2, "Checksums should not match");

    auto origin = sync1.get_string("origin");
    auto nodes1 = sync1.get_object("nodes");
    auto nodes2 = sync2.get_object("nodes");

    test.expect(nodes1 == nodes2,
                "Both MaxScales should have the same \"nodes\" data: %s",
                get_diff(nodes1, nodes2).c_str());

    int error = 0;
    int ok = 0;

    for (const auto& key : nodes1.keys())
    {
        auto value = nodes1.get_string(key);

        if (value == "OK")
        {
            test.expect(key == origin, "\"nodes\" should have {\"%s\": \"OK\"}: %s",
                        key.c_str(), nodes1.to_string(NORMAL).c_str());
            ++ok;
        }
        else
        {
            test.expect(key != origin,
                        "\"nodes\" should not have {\"%s\": \"OK\"}: %s",
                        key.c_str(), nodes1.to_string(NORMAL).c_str());
            ++error;
        }
    }

    test.expect(ok == 1, "One node should be in sync, got %d", ok);
    test.expect(error == 1, "One node should fail, got %d", error);

    test.tprintf("Restart the second MaxScale and check that the good cached configuration is used");
    test.maxscale2->restart();
    version2 = get_version(api2);
    test.expect(version2 == version1, "Expected version %ld after restart, got %ld", version1, version2);

    test.tprintf("Fix the second MaxScale and do a configuration change that works");
    test.maxscale2->ssh_node(CREATE_DIR, false);

    test.maxscale->maxctrl("alter service RW-Split-Router max_sescmd_history 20");

    wait_for_sync();

    sync1 = get(api1, "maxscale", "/data/attributes/config_sync");
    sync2 = get(api2, "maxscale", "/data/attributes/config_sync");

    test.expect(sync1 == sync2, "Expected \"config_sync\" values to be equal: %s",
                get_diff(sync1, sync2).c_str());

    res = test.maxscale->maxctrl("destroy filter test-filter");
    test.expect(res.rc == 0, "Destroying the filter should work");
    version1 = sync1.get_int("version");
    expect_sync(test, version1 + 1, 2);

    // Remove the directory in case we repeat the test
    test.maxscale->ssh_node(REMOVE_DIR, false);
    test.maxscale2->ssh_node(REMOVE_DIR, false);

    test.tprintf("Make /var/lib/maxscale unwritable, update should still succeed");
    auto version_start = get_version(api1);
    test.maxscale->ssh_node("chown root:root /var/lib/maxscale", true);
    res = test.maxscale->maxctrl("alter service RW-Split-Router max_sescmd_history 21");
    test.expect(res.rc == 0, "Command should succeed even if the config cannot be saved");

    wait_for_sync(version_start + 1);
    expect_sync(test, version_start + 1, 2);
    expect_equal(test, "services/RW-Split-Router", "/data/attributes/parameters");

    test.tprintf("Make /var/lib/maxscale writable again, update should work on both nodes");
    test.maxscale->ssh_node("chown maxscale:maxscale /var/lib/maxscale", true);
    res = test.maxscale->maxctrl("alter service RW-Split-Router max_sescmd_history 22");
    test.expect(res.rc == 0, "Command should work: %s", res.output.c_str());
    expect_sync(test, version_start + 2, 2);
    expect_equal(test, "services/RW-Split-Router", "/data/attributes/parameters");

    reset(test);
}

void test_failures(TestConnections& test)
{
    int value = 10;
    int version = 1;
    auto config_update = [&](auto mxs) {
            auto rv = mxs->maxctrl("alter service RW-Split-Router max_sescmd_history "
                                   + std::to_string(value++));
            test.expect(rv.rc == 0, "Expected alter service to work: %s", rv.output.c_str());
            expect_sync(test, version++, 2);
            expect_equal(test, "services/RW-Split-Router", "/data/attributes/parameters");
        };

    config_update(test.maxscale);

    test.tprintf("Switch master to server2");
    auto res = test.maxscale->maxctrl("call command mariadbmon switchover MariaDB-Monitor server2");
    test.expect(res.rc == 0, "Error: %s", res.output.c_str());
    config_update(test.maxscale);

    test.tprintf("Switch master to server3");
    res = test.maxscale->maxctrl("call command mariadbmon switchover MariaDB-Monitor server3");
    test.expect(res.rc == 0, "Error: %s", res.output.c_str());
    config_update(test.maxscale);

    test.tprintf("Switch master back over to server1");
    res = test.maxscale->maxctrl("call command mariadbmon switchover MariaDB-Monitor server1");
    test.expect(res.rc == 0, "Error: %s", res.output.c_str());
    config_update(test.maxscale);

    test.tprintf("Config updates should fail if all nodes are down");
    test.repl->stop_nodes();
    res = test.maxscale->maxctrl("alter service RW-Split-Router max_sescmd_history "
                                 + std::to_string(value++));
    test.expect(res.rc != 0, "Command should fail when all servers are down");

    test.tprintf("Config updates works with --skip-sync");
    res = test.maxscale->maxctrl("alter service --skip-sync RW-Split-Router max_sescmd_history "
                                 + std::to_string(value++));
    test.expect(res.rc == 0, "Command with --skip-sync should work: %s", res.output.c_str());
    test.repl->start_nodes();

    test.tprintf("Next update should override change done with --skip-sync");
    test.maxscale->wait_for_monitor();
    expect_equal(test, "maxscale", "/data/attributes/config_sync/version");
    config_update(test.maxscale2);
    expect_equal(test, "services/RW-Split-Router", "/data/attributes/parameters");

    res = test.maxscale->maxctrl("destroy service --skip-sync --force RW-Split-Router");
    test.expect(res.rc == 0, "Command with --skip-sync should work: %s", res.output.c_str());
    res = test.maxscale2->maxctrl("alter service RW-Split-Router max_sescmd_history "
                                  + std::to_string(value++));
    test.expect(res.rc == 0, "Normal command after --skip-sync should work: %s", res.output.c_str());
    ++version;

    config_update(test.maxscale2);
    expect_equal(test, "services/RW-Split-Router", "/data/attributes/parameters");

    test.tprintf("Set the version field in the database to 1, new changes should fail");
    auto c = test.repl->get_connection(0);
    c.connect();
    c.query("UPDATE mysql.maxscale_config SET version = 1");
    res = test.maxscale->maxctrl("alter service RW-Split-Router max_sescmd_history "
                                 + std::to_string(value++));
    test.expect(res.rc != 0, "Command should fail database has stale version value");

    std::string EXPECTED = "100";
    test.tprintf("Set the version field in the database to %s, all nodes should re-apply the config",
                 EXPECTED.c_str());
    c.query("UPDATE mysql.maxscale_config SET version = " + EXPECTED);
    wait_for_sync(100);
    auto mxs_version = get_version(api1);
    auto db_version = c.field("SELECT version FROM mysql.maxscale_config");

    test.expect(db_version == EXPECTED,
                "Version in the database should be %s, not %s", EXPECTED.c_str(), db_version.c_str());
    test.expect(mxs_version == std::stoi(EXPECTED),
                "Config change should update version value to %s, not %ld", EXPECTED.c_str(), mxs_version);
    expect_equal(test, "maxscale", "/data/attributes/config_sync/version");

    test.tprintf("Config change after new version should work");
    version = 101;
    config_update(test.maxscale);

    test.tprintf("Delete configuration from database, next update should recreate the row");
    c.query("DELETE FROM mysql.maxscale_config");
    config_update(test.maxscale);
    mxs_version = get_version(api1);
    db_version = c.field("SELECT version FROM mysql.maxscale_config");
    test.expect(db_version == std::to_string(mxs_version),
                "Database and MaxScale should be in sync: %s != %ld",
                db_version.c_str(), mxs_version);

    test.tprintf("Store bad configation data in database");
    c.query("ALTER TABLE mysql.maxscale_config MODIFY COLUMN config TEXT");
    c.query("UPDATE mysql.maxscale_config SET config = 'hello world', version = 105");
    wait_for_sync(105);
    mxs_version = get_version(api1);
    test.expect(mxs_version != 105, "Configuration with bad JSON should not increment version");

    reset(test);
}

void test_bad_cache(TestConnections& test)
{
    auto expect_empty = [&]() {
            auto sync1 = get(api1, "maxscale", "/data/attributes/config_sync");
            test.expect(sync1.type() == JsonType::JSON_NULL,
                        "Wrong cached configuration should not be read.");
        };

    auto expect_discarded = [&]() {
            int rc = test.maxscale->ssh_node("test -f /var/lib/maxscale/maxscale-config.json", true);
            test.expect(rc != 0, "Bad cached configuration should be discarded");
        };

    test.tprintf("Create a cached configuration with no monitor");
    std::string NO_MONITOR =
        R"EOF({"config":[{"id":"server1","type":"servers","attributes":{"parameters":{"port":3306,"address":"127.0.0.1"}}}],"version":2,"cluster_name":"MariaDB-Monitor"})EOF";
    create_config(test.maxscale, NO_MONITOR);
    expect_empty();
    expect_discarded();

    test.tprintf("Create a cached configuration for the wrong cluster");
    std::string WRONG_CONFIG =
        R"EOF({"config":[{"id":"server1","type":"servers","attributes":{"parameters":{"port":3306,"address":"127.0.0.1"}}}],"version":2,"cluster_name":"Other-Cluster"})EOF";
    create_config(test.maxscale, WRONG_CONFIG);
    expect_empty();

    test.tprintf("Create a bad cached configuration and make sure it's discarded");
    std::string BAD_CONFIG =
        R"EOF({"config":[{"id":"server1","type":"servers","attributes":{"parameters":{"rank":"tertiary"}}}],"version":123,"cluster_name":"MariaDB-Monitor"})EOF";
    create_config(test.maxscale, BAD_CONFIG);
    expect_empty();
    expect_discarded();

    reset(test);
}

void test_conflicts(TestConnections& test)
{
    // Each test case should increment the version by one
    int version = 0;

    test.tprintf("Create a filter");
    test.check_maxctrl("create filter test-object hintfilter");
    ++version;

    expect_sync(test, version, 2);
    expect_equal(test, "filters/test-object", "/data/type");

    test.tprintf("Stop the second MaxScale");
    test.maxscale2->stop();

    test.tprintf("Recreate the filter as a server");
    test.check_maxctrl("destroy filter test-object");
    ++version;
    test.check_maxctrl("create server test-object 127.0.0.1 3306");
    ++version;

    test.tprintf("Start the second MaxScale: it should destroy the filter and create it as a server");
    test.maxscale2->start();

    expect_sync(test, version, 2);
    expect_equal(test, "servers/test-object", "/data/type");

    test.tprintf("Destroy the server");
    test.check_maxctrl("destroy server test-object");
    ++version;
    expect_sync(test, version, 2);

    test.tprintf("Create the object as a service");
    test.check_maxctrl("create service test-object readwritesplit user=maxskysql password=skysql");
    ++version;

    expect_sync(test, version, 2);
    expect_equal(test, "services/test-object", "/data/attributes/router");

    test.tprintf("Stop the second MaxScale");
    test.maxscale2->stop();

    test.tprintf("Destroy the service and create it with another router");
    test.check_maxctrl("destroy service test-object");
    ++version;
    test.check_maxctrl("create service test-object readconnroute user=maxskysql password=skysql");
    ++version;

    test.tprintf("Start the second MaxScale: it should recreate the service");
    test.maxscale2->start();

    expect_sync(test, version, 2);
    expect_equal(test, "services/test-object", "/data/attributes/router");

    test.tprintf("Destroy the service and create a qlafilter");
    test.check_maxctrl("destroy service test-object");
    ++version;
    test.check_maxctrl("create filter test-object qlafilter filebase=/tmp/file1");
    ++version;

    expect_sync(test, version, 2);
    expect_equal(test, "filters/test-object", "/data/attributes/parameters");

    test.tprintf("Stop the second MaxScale");
    test.maxscale2->stop();

    // TODO: The filter needs to be changed when runtime config change support is added to qlafilter
    test.tprintf("Destroy the filter and create it with different parameters");
    test.check_maxctrl("destroy filter test-object");
    ++version;
    test.check_maxctrl("create filter test-object qlafilter filebase=/tmp/file2");
    ++version;

    test.tprintf("Start the second MaxScale: it should recreate the filter");
    test.maxscale2->start();

    expect_sync(test, version, 2);
    expect_equal(test, "filters/test-object", "/data/attributes/parameters");

    reset(test);
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    api1 = create_api1(test);
    api2 = create_api2(test);

    test.log_printf("1. test_config_parameters");
    test_config_parameters(test);

    test.log_printf("2. test_sync");
    test_sync(test);

    test.log_printf("3. test_bad_change");
    test_bad_change(test);

    test.log_printf("4. test_failures");
    test_failures(test);

    test.log_printf("5. test_bad_cache");
    test_bad_cache(test);

    test.log_printf("6. test_conflicts");
    test_conflicts(test);

    return test.global_result;
}
