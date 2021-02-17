/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <string>
#include <vector>
#include <maxbase/http.hh>
#include <maxscale/jansson.hh>

namespace http = mxb::http;
using namespace std;

void wait_for_monitor_loop()
{
    sleep(2);
}

class Exception : public std::runtime_error
{
    using std::runtime_error::runtime_error;
};

#define REQUIRE(condition, command)\
    do { if (!(command)) { string s("Requirement '"); s+=condition;s+='"';\
            throw Exception(s); } } while (false)

namespace cs
{
const char ZCLUSTER_MODE[] = "cluster_mode";
const char ZSERVICES[] = "services";
const char ZTIMEOUT[]  = "timeout";

namespace body
{
string shutdown(int timeout)
{
    stringstream ss;
    ss << "{"
       << "\"" << ZTIMEOUT << "\": "
       << timeout
       << "}";

    return ss.str();
}
}
}

class MaxCtrl
{
public:
    MaxCtrl(const string& path)
        : m_path(path)
    {
    }

    enum class Output
    {
        TSV,
        RAW
    };

    vector<string> command(const std::string& maxctrl_command, Output output) const
    {
        vector<string> rows;
        string command = m_path;
        command += " ";

        if (output == Output::TSV)
        {
            command += "--tsv ";
        }

        command += maxctrl_command;

        FILE* pF = popen(command.c_str(), "r");

        if (pF)
        {
            string row;

            while (!feof(pF))
            {
                int c = fgetc(pF);

                switch (c)
                {
                case '\n':
                    if (output == Output::TSV)
                    {
                        mxb::trim(row); // Remove trailing tab
                        rows.push_back(row);
                        row.clear();
                    }
                    break;

                case -1:
                    break;

                default:
                    row.push_back(c);
                }
            }

            if (!row.empty())
            {
                rows.push_back(row);
            }

            pclose(pF);
        }
        else
        {
            throw Exception(strerror(errno));
        }

        return rows;
    }

    vector<string> list_servers() const
    {
        return command("list servers", Output::TSV);
    }

    static string get_status_from_server_row(const string& row)
    {
        string status;
        auto pos = row.find_last_of('\t');
        if (pos != string::npos)
        {
            status = row.substr(pos + 1);
            mxb::trim(status);
        }
        else
        {
            cout << "Unexpected server row: " << row << endl;
        }

        return status;
    }

    static int check_status_from_server_row(const string& row, const char* zExpectation)
    {
        int rv = 1;
        auto status = MaxCtrl::get_status_from_server_row(row);
        if (status != zExpectation)
        {
            cout << "Expected status to be '" << zExpectation << "', but it was: " << status << endl;
            rv = 1;
        }
        else
        {
            cout << "Server is '" << zExpectation << "' as expected." << endl;
            rv = 0;
        }

        return rv;
    }

    enum class Mode
    {
        READONLY,
        READWRITE
    };

    void set_mode(Mode mode) const
    {
        string s("call command csmon mode-set CSMonitor ");

        if (mode == Mode::READONLY)
        {
            s += "readonly";
        }
        else
        {
            s += "readwrite";
        }

        s += " 10s";

        command(s, Output::RAW);
    }

private:
    std::string m_path;
};

const char ZBASE_PATH[] = "/cmapi/0.4.0/node";
const char ZPORT[] = "8640";

unique_ptr<json_t> load_json(const string& json)
{
    json_error_t error;
    unique_ptr<json_t> sJson(json_loadb(json.c_str(), json.length(), 0, &error));

    if (!sJson)
    {
        throw Exception(error.text);
    }

    return sJson;
}

class CSTest
{
public:
    CSTest(const http::Config& config,
           const string& address)
        : m_config(config)
        , m_address(address)
    {
    }

    static string get_url(const string& address, const string& command)
    {
        return string("https://") + address + ":" + ZPORT + ZBASE_PATH + "/" + command;
    }

    string get_url(const string& command) const
    {
        return get_url(m_address, command);
    }

    http::Response get_status_response(const string& address) const
    {
        string url = get_url(address, "status");

        return http::get(url, m_config);
    }

    http::Response get_status_response() const
    {
        return get_status_response(m_address);
    }

    bool is_cluster_down(const string& address) const
    {
        auto response = get_status_response(address);

        auto sJson = load_json(response.body);

        json_t* pServices = json_object_get(sJson.get(), cs::ZSERVICES);

        return json_array_size(pServices) == 0;
    }

    bool is_cluster_down() const
    {
        return is_cluster_down(m_address);
    }

    enum class Mode
    {
        READONLY,
        READWRITE
    };

    Mode get_mode(const string& address) const
    {
        auto response = get_status_response(address);
        auto sJson = load_json(response.body);
        json_t* pCluster_mode = json_object_get(sJson.get(), cs::ZCLUSTER_MODE);
        const char* zCluster_mode = json_string_value(pCluster_mode);

        if (strcmp(zCluster_mode, "readonly") == 0)
        {
            return Mode::READONLY;
        }
        else if (strcmp(zCluster_mode, "readwrite") == 0)
        {
            return Mode::READWRITE;
        }
        else
        {
            REQUIRE("Cluster-mode is readonly or readwrite.", false);
        }

        return Mode::READONLY;
    }

    Mode get_mode() const
    {
        return get_mode(m_address);
    }

    bool shutdown(const string& address) const
    {
        auto url = get_url(address, "shutdown");
        auto body = cs::body::shutdown(25);

        auto config = m_config;
        config.timeout = std::chrono::seconds(30);

        auto response = http::put(url, body, m_config);

        return response.is_success();
    }

    bool shutdown() const
    {
        return shutdown(m_address);
    }

    bool start(const string& address) const
    {
        auto url = get_url(address, "start");

        auto response = http::put(url, "{}", m_config);

        return response.is_success();
    }

    bool start() const
    {
        return start(m_address);
    }


private:
    http::Config m_config;
    string       m_address;
};

namespace test
{

void start_or_shutdown(CSTest& cs, MaxCtrl& maxctrl)
{
    if (cs.is_cluster_down())
    {
        cout << "Cluster is not running, starting." << endl;
        cs.start();
        wait_for_monitor_loop();

        REQUIRE("Cluster is running.", !cs.is_cluster_down());
    }
    else
    {
        cout << "Cluster is running, shutting down." << endl;
        cs.shutdown();
        wait_for_monitor_loop();

        REQUIRE("Cluster is shut down.", cs.is_cluster_down());
    }
}

int can_start_and_shutdown_cluster(CSTest& cs, MaxCtrl& maxctrl)
{
    cout << "\nCan start and shutdown cluster." << endl;

    start_or_shutdown(cs, maxctrl);
    start_or_shutdown(cs, maxctrl);

    return 0;
}

int compare_returned_statuses(CSTest& cs, MaxCtrl& maxctrl)
{
    auto response = cs.get_status_response();
    auto rows = maxctrl.command("call command csmon status CSMonitor", MaxCtrl::Output::RAW);
    REQUIRE("status returns 1 row", rows.size() == 1);

    auto sStatus1 = load_json(response.body);
    json_object_del(sStatus1.get(), "timestamp"); // The timestamp will be different, so we drop it.
    json_object_del(sStatus1.get(), "uptime"); // The uptime will be different, so we drop it.

    auto sResult = load_json(rows.front());
    auto pMeta = json_object_get(sResult.get(), "meta");
    auto pServers = json_object_get(pMeta, "servers");
    REQUIRE("Result from one server returned.", json_array_size(pServers) == 1);
    auto pServer = json_array_get(pServers, 0);
    auto pStatus2 = json_object_get(pServer, "result");
    json_object_del(pStatus2, "timestamp");  // The timestamp will be different, so we drop it.
    json_object_del(pStatus2, "csmon_trx_active"); // Only MaxScale return object may have this.
    json_object_del(pStatus2, "uptime"); // The uptime will be different, so we drop it.

    bool rv = json_equal(sStatus1.get(), pStatus2) == 1 ? 0 : 1;

    if (rv != 0)
    {
        cout << "\nsStatus1" << endl;
        cout << mxs::json_dump(sStatus1.get(), JSON_INDENT(4)) << endl;

        cout << "\npStatus1" << endl;
        cout << mxs::json_dump(pStatus2, JSON_INDENT(4)) << endl;
    }

    return rv;
}

int can_maxscale_return_status(CSTest& cs, MaxCtrl& maxctrl)
{
    int rv = 0;
    cout << "\nCan maxscale return status." << endl;

    rv += compare_returned_statuses(cs, maxctrl);
    start_or_shutdown(cs, maxctrl);
    rv += compare_returned_statuses(cs, maxctrl);

    return rv;
}

int can_maxscale_change_mode(CSTest& cs, MaxCtrl& maxctrl)
{
    cout << "\nCan MaxScale change mode." << endl;

    string command("call command csmon mode-set CSMonitor ");

    auto mode1 = cs.get_mode();
    if (mode1 == CSTest::Mode::READONLY)
    {
        maxctrl.set_mode(MaxCtrl::Mode::READWRITE);
    }
    else
    {
        maxctrl.set_mode(MaxCtrl::Mode::READONLY);
    }

    auto mode2 = cs.get_mode();

    return mode1 != mode2 ? 0 : 1;
}

int detects_that_cluster_is_down(CSTest& cs, MaxCtrl& maxctrl)
{
    cout << "\nDetects that cluster is down." << endl;

    if (!cs.is_cluster_down())
    {
        cout << "Cluster is running, shutting down." << endl;
        cs.shutdown();
        wait_for_monitor_loop();
    }

    REQUIRE("Cluster is down.", cs.is_cluster_down());

    int rv = 0;

    auto rows = maxctrl.list_servers();
    for (auto& row : rows)
    {
        rv += MaxCtrl::check_status_from_server_row(row, "Down");
    }

    return rv;
}

int detects_that_cluster_is_up(CSTest& cs, MaxCtrl& maxctrl)
{
    cout << "\nDetects that cluster is up." << endl;

    if (cs.is_cluster_down())
    {
        cout << "Cluster is shut down, starting." << endl;
        cs.start();
        wait_for_monitor_loop();
    }

    REQUIRE("Cluster is up.", !cs.is_cluster_down());

    int rv = 0;

    auto rows = maxctrl.list_servers();
    for (auto& row : rows)
    {
        rv += MaxCtrl::check_status_from_server_row(row, "Master, Running");
    }

    return rv;
}

int detects_when_cluster_goes_down(CSTest& cs, MaxCtrl& maxctrl)
{
    cout << "\nDetects when cluster goes down." << endl;

    if (cs.is_cluster_down())
    {
        cout << "Cluster is shut down, starting." << endl;
        cs.start();
        wait_for_monitor_loop();
    }

    REQUIRE("Cluster is up.", !cs.is_cluster_down());

    int rv = 0;

    auto rows = maxctrl.list_servers();
    for (auto& row : rows)
    {
        rv += MaxCtrl::check_status_from_server_row(row, "Master, Running");
    }

    if (rv == 0)
    {
        cs.shutdown();
        wait_for_monitor_loop();

        REQUIRE("Cluster is down.", cs.is_cluster_down());

        auto rows = maxctrl.list_servers();
        for (auto& row : rows)
        {
            rv += MaxCtrl::check_status_from_server_row(row, "Down");
        }
    }

    return rv;
}

}

void print_usage_and_exit(const char* zProgram)
{
    cout << "usage: " << zProgram << " <maxctrl-path> <api-key> <server-address>" << endl;
    exit(EXIT_FAILURE);
}

int main(int argc, char* argv[])
{
    if (argc != 4)
    {
        print_usage_and_exit(argv[0]);
    }

    int rv = 0;

    string maxctrl_path = argv[1];
    string api_key = argv[2];
    string server_address = argv[3];

    http::Config config;
    config.headers["X-API-KEY"] = api_key;
    config.headers["Content-Type"] = "application/json";

    // The CS daemon uses a self-signed certificate.
    config.ssl_verifypeer = false;
    config.ssl_verifyhost = false;

    CSTest cs(config, server_address);
    MaxCtrl maxctrl(maxctrl_path);

    try
    {
        rv += test::can_start_and_shutdown_cluster(cs, maxctrl);
        rv += test::can_maxscale_return_status(cs, maxctrl);
        rv += test::can_maxscale_change_mode(cs, maxctrl);
        rv += test::detects_that_cluster_is_down(cs, maxctrl);
        rv += test::detects_that_cluster_is_up(cs, maxctrl);
        rv += test::detects_when_cluster_goes_down(cs, maxctrl);
    }
    catch (const std::exception& x)
    {
        cout << "Exception: " << x.what() << endl;
        ++rv;
    }

    return rv;
}
