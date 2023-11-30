/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <fstream>

void query(TestConnections& test, const std::vector<std::string>& queries)
{
    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Failed to connect: %s", c.error());

    for (const auto& q : queries)
    {
        test.expect(c.query(q), "Failed to execute query '%s': %s", q.c_str(), c.error());
    }
}

void send_query(TestConnections& test, const std::vector<std::string>& queries)
{
    auto c = test.maxscale->rwsplit();
    test.expect(c.connect(), "Failed to connect: %s", c.error());

    for (const auto& q : queries)
    {
        test.expect(c.send_query(q), "Failed to send query '%s': %s", q.c_str(), c.error());
    }

    for (const auto& q : queries)
    {
        test.expect(c.read_query_result(), "Failed to read query result '%s': %s", q.c_str(), c.error());
    }
}

std::vector<std::vector<std::string>> parse_log(TestConnections& test, const std::string& log)
{
    std::vector<std::vector<std::string>> rval;
    test.maxscale->copy_from_node(log.c_str(), "./log.txt");
    std::ifstream infile("log.txt");

    for (std::string line; std::getline(infile, line);)
    {
        rval.push_back(mxb::strtok(line, ","));
    }

    remove("log.txt");

    return rval;
}

// Rows and fields are zero indexed but the first row contains the header.
void check_contents(TestConnections& test, const std::string& file,
                    std::vector<std::tuple<int, int, std::string>> expected_rows)
{
    auto contents = parse_log(test, file);

    for (const auto& expected : expected_rows)
    {
        int row;
        int col;
        std::string line;
        std::tie(row, col, line) = expected;

        try
        {
            auto field = contents.at(row).at(col);
            test.expect(field == line,
                        "Expected row %d col %d to be '%s', not '%s'",
                        row, col, line.c_str(), field.c_str());
        }
        catch (const std::exception& e)
        {
            test.add_failure("Row %d col %d does not exist: %s", row, col, e.what());
        }
    }
}

void test_user_matching(TestConnections& test)
{
    test.check_maxctrl("alter filter QLA "
                       "log_type=unified filebase=/tmp/qla.log.user_match  use_canonical_form=false "
                       "user_match=/bob/ user_exclude=/bobby/ log_data=query");

    test.maxscale->restart();

    query(test, {"CREATE USER 'alice' IDENTIFIED BY 'alice'", "GRANT ALL ON *.* TO 'alice'",
                 "CREATE USER 'bob' IDENTIFIED BY 'bob'", "GRANT ALL ON *.* TO 'bob'",
                 "CREATE USER 'bobby' IDENTIFIED BY 'bobby'", "GRANT ALL ON *.* TO 'bobby'"});

    // Make sure that the users have replicated over and that MaxScale has loaded them
    test.repl->sync_slaves();
    test.check_maxctrl("reload service RW-Split-Router");

    auto c = test.maxscale->rwsplit();

    // Do the query first with the excluded user, this way if it ends up matching it'll be detected
    c.set_credentials("bobby", "bobby");
    test.expect(c.connect() && c.query("SELECT 'bobby'"), "Query with 'bobby' should work: %s", c.error());

    c.set_credentials("alice", "alice");
    test.expect(c.connect() && c.query("SELECT 'alice'"), "Query with 'alice' should work: %s", c.error());

    c.set_credentials("bob", "bob");
    test.expect(c.connect() && c.query("SELECT 'bob'"), "Query with 'bob' should work: %s", c.error());

    check_contents(test, "/tmp/qla.log.user_match.unified", {
        {1, 0, "SELECT 'bob'"}
    });

    query(test, {"DROP USER 'alice'", "DROP USER 'bob'", "DROP USER 'bobby'"});
}

void test_source_matching(TestConnections& test)
{
    auto run_query = [&](int node, int value){
        test.repl->ssh_node_f(node, true, "mariadb -u maxskysql -pskysql -h %s -P 4006 -e \"SELECT %d\"",
                              test.maxscale->ip(), value);
    };

    auto match = mxb::cat("source_match=/(", test.repl->ip(0), ")|(", test.repl->ip(1), ")/");
    auto exclude = mxb::cat("source_exclude=/", test.repl->ip(0), "/");

    test.check_maxctrl("alter filter QLA log_data=query log_type=unified filebase=/tmp/qla.log.source_match "
                       "user_match=\"\" user_exclude=\"\" use_canonical_form=false "
                       "\"" + match + "\" \"" + exclude + "\"");

    test.maxscale->restart();

    for (int i = 0; i < test.repl->N; i++)
    {
        run_query(i, i);
    }

    check_contents(test, "/tmp/qla.log.source_match.unified", {
        {1, 0, "SELECT 1"}
    });
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    // Clean up old files
    test.maxscale->ssh_node("rm -f /tmp/qla.log.*", true);


    test.tprintf("Test log_type=session");

    // Each session should have a separate file
    query(test, {"SELECT 'session-log-1'"});
    query(test, {"SELECT 'session-log-2'"});
    check_contents(test, "/tmp/qla.log.1", {{1, 2, "SELECT 'session-log-1'"}});
    check_contents(test, "/tmp/qla.log.2", {{1, 2, "SELECT 'session-log-2'"}});


    test.tprintf("Test log_type=unified");

    test.check_maxctrl("alter filter QLA log_type=unified");

    query(test, {"SELECT 'unified-log'", "SELECT 'unified-log-2'"});
    check_contents(test, "/tmp/qla.log.unified", {
        {1, 2, "SELECT 'unified-log'"},
        {2, 2, "SELECT 'unified-log-2'"}
    });


    test.tprintf("Test SQL matching");

    test.check_maxctrl("alter filter QLA match=/something\\|anything/ "
                       "filebase=/tmp/qla.match.log");

    query(test, {"SELECT 'nothing'", "SELECT 'something'", "SELECT 'everything'", "SELECT 'anything'"});
    check_contents(test, "/tmp/qla.match.log.unified", {
        {1, 2, "SELECT 'something'"},
        {2, 2, "SELECT 'anything'"}
    });

    test.tprintf("Test SQL matching with pipelined queries");

    send_query(test, {"SELECT 'something'", "SELECT 'nothing'", "SELECT 'everything'", "SELECT 'anything'"});
    check_contents(test, "/tmp/qla.match.log.unified", {
        {1, 2, "SELECT 'something'"},
        {2, 2, "SELECT 'anything'"},
        {3, 2, "SELECT 'something'"},
        {4, 2, "SELECT 'anything'"}
    });

    test.maxscale->ssh_node("rm -f /tmp/qla.match.log.unified", true);
    test.check_maxctrl("alter filter QLA match=/.*/");


    test.tprintf("Test filebase=/tmp/qla.second.log");

    test.check_maxctrl("alter filter QLA filebase=/tmp/qla.second.log");

    query(test, {"SELECT 'second-log'"});
    check_contents(test, "/tmp/qla.second.log.unified", {
        {1, 2, "SELECT 'second-log'"}
    });

    test.check_maxctrl("alter filter QLA filebase=/tmp/qla.log");
    test.maxscale->ssh_node("rm -f /tmp/qla.second.log.unified", true);


    test.tprintf("Test use_canonical_form=true");

    test.maxscale->ssh_node("truncate -s 0 /tmp/qla.log.unified", true);
    test.check_maxctrl("alter filter QLA use_canonical_form=true");

    query(test, {"SELECT 'canonical'", "SELECT 'canonical' field_name"});
    check_contents(test, "/tmp/qla.log.unified", {
        {1, 2, "SELECT ?"},
        {2, 2, "SELECT ? field_name"}
    });

    test.check_maxctrl("alter filter QLA use_canonical_form=false");


    test.tprintf("Test log_data=reply_time");

    test.maxscale->ssh_node("truncate -s 0 /tmp/qla.log.unified", true);
    test.check_maxctrl("alter filter QLA log_data=reply_time");

    query(test, {"SELECT SLEEP(0.1)"});
    auto log = parse_log(test, "/tmp/qla.log.unified");

    try
    {
        int ms = std::stoi(log.at(1).at(0));
        test.expect(ms >= 100, "Expected query to take >= 100ms, not %dms", ms);
    }
    catch (const std::exception& e)
    {
        test.add_failure("Failed to parse reply time: %s", e.what());
    }

    test.log_printf("Test user_match and user_exclude");
    test_user_matching(test);

    test.log_printf("Test source_match and source_exclude");
    test_source_matching(test);

    // Removes the files that were created
    test.maxscale->stop();
    test.maxscale->ssh_node("rm -f /tmp/qla.log.*", true);

    return test.global_result;
}
