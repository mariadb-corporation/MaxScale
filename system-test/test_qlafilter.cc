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


    // Removes the files that were created
    test.maxscale->stop();
    test.maxscale->ssh_node("rm -f /tmp/qla.log.*", true);

    return test.global_result;
}
