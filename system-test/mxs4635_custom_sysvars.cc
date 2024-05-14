/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <sstream>

std::map<std::string, std::string> get_system_variables(TestConnections& test)
{
    std::map<std::string, std::string> rval;
    test.maxscale->connect_rwsplit();
    MYSQL* conn = test.maxscale->conn_rwsplit;

    const char* keyptr = nullptr;
    size_t keylen = 0;
    const char* valueptr = nullptr;
    size_t valuelen = 0;

    if (!mysql_session_track_get_first(conn, SESSION_TRACK_SYSTEM_VARIABLES, &keyptr, &keylen)
        && !mysql_session_track_get_next(conn, SESSION_TRACK_SYSTEM_VARIABLES, &valueptr, &valuelen))
    {
        rval.emplace(std::string_view(keyptr, keylen), std::string_view(valueptr, valuelen));

        while (!mysql_session_track_get_next(conn, SESSION_TRACK_SYSTEM_VARIABLES, &keyptr, &keylen)
               && !mysql_session_track_get_next(conn, SESSION_TRACK_SYSTEM_VARIABLES, &valueptr, &valuelen))
        {
            rval.emplace(std::string_view(keyptr, keylen), std::string_view(valueptr, valuelen));
        }
    }

    test.maxscale->close_rwsplit();

    return rval;
}

int num_conns(TestConnections& test, int expected)
{
    auto vars = get_system_variables(test);
    MXT_EXPECT_F(!vars["threads_connected"].empty(), "No 'threads_connected' variable found.");

    int n = atoi(vars["threads_connected"].c_str());
    MXT_EXPECT_F(n > 0, "Value of 'threads_connected' should be positive: %d", n);

    // The counters are decremented when the objects in MaxScale are destroyed and this isn't guaranteed
    // to have happened when the disconnection completes on the client side. Retry for a short while to
    // give the counters some time to stabilize.
    for (int retries = 0; retries < 30 && n != expected; retries++)
    {
        std::this_thread::sleep_for(100ms);
        vars = get_system_variables(test);
        n = atoi(vars["threads_connected"].c_str());
    }

    return n;
}

void test_connection_counts(TestConnections& test)
{
    std::vector<Connection> conns;
    int baseline = num_conns(test, 1);
    MXT_EXPECT_F(baseline == 1, "Expecting one connection, got %d", baseline);

    constexpr int NUM_CLIENTS = 25;

    for (int i = 0; i < NUM_CLIENTS; i++)
    {
        conns.push_back(test.maxscale->rwsplit());
        MXT_EXPECT_F(conns.back().connect(), "Failed to connect: %s", conns.back().error());

        // Do one query to make sure the connection count has been updated
        MXT_EXPECT_F(conns.back().query("SELECT 1"), "Failed to query: %s", conns.back().error());

        int n_conns = num_conns(test, baseline + 1);
        MXT_EXPECT_F(baseline + 1 == n_conns, "Connect %d: expected %d connections but got %d",
                     i + 1, baseline + 1, n_conns);
        ++baseline;
    }

    for (int i = 0; i < NUM_CLIENTS; i++)
    {
        conns[i].disconnect();

        int n_conns = num_conns(test, baseline - 1);
        MXT_EXPECT_F(baseline - 1 == n_conns, "Disconnect %d: Expected %d connections but got %d",
                     i + 1, baseline - 1, n_conns);
        --baseline;
    }
}


std::map<std::string, std::string> update_and_get_variables(TestConnections& test, std::string value)
{
    test.check_maxctrl("alter listener RW-Split-Listener connection_metadata=" + value);
    // The "show threads" should help avoid the unlikely case where a worker hasn't received the new
    // version of the config when it accepts this client. By pinging all workers, we make sure prior
    // messages have been processed.
    test.check_maxctrl("show threads");
    // Waiting two monitor intervals makes sure the variables get updated
    test.maxscale->wait_for_monitor(2);
    return get_system_variables(test);
}

void test_custom_metadata(TestConnections& test)
{
    auto vars = get_system_variables(test);

    auto check = [&](std::string key, std::string value){
        MXT_EXPECT_F(vars[key] == value, "Expected '%s' to be '%s' but it was '%s'",
                     key.c_str(), value.c_str(), vars[key].c_str());
    };

    auto update_value = [&](std::string value){
        vars = update_and_get_variables(test, value);
    };

    // Count how many values are in the default value. This makes the test adapt to the number of expected
    // parameters if the defaults are changed.
    auto res = test.maxctrl("api get listeners/RW-Split-Listener "
                            "data.attributes.parameters.connection_metadata");
    const size_t num_default = 1 + std::count(res.output.begin(), res.output.end(), ',');

    // Some values are always added in the first OK packet. The number of these can be deduced from the total
    // number of variables.
    const size_t num_always = vars.size() - num_default;

    // Baseline. Don't check "threads_connected" since it's not guaranteed to be 1 if the connections from the
    // previous test are still being closed.
    MXT_EXPECT_F(vars.size() > num_default,
                 "Expected more than %d values, got %lu", num_default, vars.size());

    // One custom value
    update_value("hello=world");
    MXT_EXPECT(vars.size() == 1 + num_always);
    check("hello", "world");

    // Reset to empty
    update_value("\"\"");
    MXT_EXPECT(vars.size() == num_always);

    // Override a value generated by MaxScale
    update_value("threads_connected=enough");
    MXT_EXPECT(vars.size() == num_always);
    check("threads_connected", "enough");

    // Change to a different value
    update_value("some=thing");
    MXT_EXPECT(vars.size() == 1 + num_always);
    check("some", "thing");

    // Two values
    update_value("hello=world,some=thing");
    MXT_EXPECT(vars.size() == 2 + num_always);
    check("hello", "world");
    check("some", "thing");

    // Three values
    update_value("hello=world,some=thing,too=many=variables=in=one=string");
    MXT_EXPECT(vars.size() == 3 + num_always);
    check("hello", "world");
    check("some", "thing");
    check("too", "many=variables=in=one=string");

    // JDBC connection URL as a value
    update_value("redirect_url=jdbc:mariadb://localhost:3306/test?useServerPrepStmts=true");
    MXT_EXPECT(vars.size() == 1 + num_always);
    check("redirect_url", "jdbc:mariadb://localhost:3306/test?useServerPrepStmts=true");
    check("threads_connected", "1");

    // Lots of values
    std::ostringstream ss;

    for (int i = 0; i < 1000; i++)
    {
        if (i > 0)
        {
            ss << ",";
        }

        ss << "key" << i << "=value" << i;
    }

    update_value(ss.str());
    MXT_EXPECT(vars.size() == 1000 + num_always);
    check("threads_connected", "1");

    for (int i = 0; i < 1000; i++)
    {
        auto num = std::to_string(i);
        check("key" + num, "value" + num);
    }
}

void test_auto_metadata(TestConnections& test)
{

    // One "auto" value
    auto vars = update_and_get_variables(test, "max_connections=auto");
    auto c = test.maxscale->rwsplit();
    MXT_EXPECT(c.connect());
    int expected_max_cons = atoi(c.field("SELECT @@max_connections").c_str());
    MXT_EXPECT(expected_max_cons > 0);
    int max_conns = atoi(vars["max_connections"].c_str());
    MXT_EXPECT_F(max_conns == expected_max_cons,
                 "Expected 'max_connections' to be %d, not %d",
                 expected_max_cons, max_conns);

    // Two "auto" values and one custom one
    vars = update_and_get_variables(test, "max_allowed_packet=auto,hello=world,max_connections=auto");
    int expected_max_allowed_packet = atoi(c.field("SELECT @@max_allowed_packet").c_str());
    MXT_EXPECT(expected_max_allowed_packet > 0);
    int max_allowed_packet = atoi(vars["max_allowed_packet"].c_str());
    MXT_EXPECT_F(max_allowed_packet == expected_max_allowed_packet,
                 "Expected 'max_allowed_packet' to be %d, not %d",
                 expected_max_allowed_packet, max_allowed_packet);
    MXT_EXPECT(vars["hello"] == "world");
}

void test_main(TestConnections& test)
{
    test.tprintf("Testing connection counts");
    test_connection_counts(test);

    test.tprintf("Testing custom metadata");
    test_custom_metadata(test);

    test.tprintf("Testing automatic metadata");
    test_auto_metadata(test);
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
