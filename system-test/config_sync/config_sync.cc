#include <maxtest/testconnections.hh>
#include <maxtest/maxrest.hh>
#include <maxbase/json.hh>
#include <iostream>
#include <chrono>

using namespace std::chrono;

const auto NORMAL = mxb::Json::Format::NORMAL;

using RestApi = std::unique_ptr<MaxRest>;

RestApi api1;
RestApi api2;

struct TestCase
{
    std::string desc;       // Test description
    std::string cmd;        // The MaxCtrl command to execute
    std::string endpoint;   // REST API endpoint to check, optional
    std::string ptr;        // JSON Pointer to field to check, optional

    void execute(TestConnections& test, Maxscales* maxscale) const
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
};

mxb::Json get(const RestApi& api, const std::string& endpoint, const std::string& js_ptr)
{
    mxb::Json rval(mxb::Json::Type::NONE);

    if (auto json = api->curl_get(endpoint))
    {
        rval = json.at(js_ptr.c_str());
    }

    return rval;
}

void wait_for_sync(TestConnections& test, int expected_version, size_t num_maxscales)
{
    bool ok = true;
    std::ostringstream ss;

    auto check = [&](auto status) {
            int version = status.get_int("version");
            if (version != expected_version)
            {
                ok = false;
                ss << "Expected version " << expected_version << ", got " << version << '\n';
            }

            auto nodes = status.get_object("nodes");
            size_t num_fields = json_object_size(nodes.get_json());
            if (num_fields != num_maxscales)
            {
                ok = false;
                ss << "Expected \"nodes\" object to have " << num_maxscales << " fields, got "
                   << num_fields << ": " << nodes.to_string(NORMAL) << '\n';
            }

            if (!status.contains("origin"))
            {
                ok = false;
                ss << "Expected \"origin\" to not be empty.\n";
            }

            if (!status.contains("status"))
            {
                ok = false;
                ss << "Expected \"status\" to not be empty.\n";
            }
        };

    auto start = steady_clock::now();

    while (steady_clock::now() - start < seconds(5))
    {
        ss.str("");
        ok = true;

        auto status1 = get(api1, "maxscale", "/data/attributes/config_sync");
        auto status2 = get(api2, "maxscale", "/data/attributes/config_sync");

        check(status1);
        check(status2);

        if (ok)
        {
            test.expect(status1 == status2, "Expected JSON to be equal: %s != %s",
                        status1.to_string(NORMAL).c_str(), status2.to_string(NORMAL).c_str());
            break;
        }
        else
        {
            std::this_thread::sleep_for(milliseconds(100));
        }
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

    test.expect(value1 == value2, "Values for '%s' at '%s' are not equal: %s != %s",
                resource.c_str(), path.c_str(),
                value1.to_string(NORMAL).c_str(),
                value2.to_string(NORMAL).c_str());
}

void test_config(TestConnections& test)
{
    for (auto cmd : {
        "alter maxscale config_sync_cluster some-monitor",
        "destroy monitor --force MariaDB-Monitor"
    })
    {
        test.expect(test.maxscale->maxctrl(cmd).rc != 0,
                    "Command should fail: %s", cmd);
    }
}

void test_sync(TestConnections& test)
{
    // Each test case should increment the version by one
    int version = 1;

    test.tprintf("Execute tests with both MaxScales running");

    for (const auto& t : tests)
    {
        t.execute(test, test.maxscale);
        wait_for_sync(test, version++, 2);
        expect_equal(test, t.endpoint, t.ptr);
    }

    test.tprintf("Execute tests with only one MaxScale");
    test.maxscale2->stop();

    for (const auto& t : tests)
    {
        t.execute(test, test.maxscale);
    }


    test.tprintf("Start the second MaxScale and make sure it catches up");

    version = get(api1, "maxscale", "/data/attributes/config_sync").get_int("version");
    test.maxscale2->start();
    wait_for_sync(test, version, 2);
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    api1 = std::make_unique<MaxRest>(&test, test.maxscale);
    api2 = std::make_unique<MaxRest>(&test, test.maxscale2);

    test_config(test);
    test_sync(test);

    test.tprintf("Cleanup");
    test.stop_all_maxscales();

    auto conn = test.repl->get_connection(0);
    test.expect(conn.connect(), "Connection failed: %s", conn.error());
    test.expect(conn.query("DROP TABLE mysql.maxscale_config"), "DROP failed: %s", conn.error());
    return test.global_result;
}
