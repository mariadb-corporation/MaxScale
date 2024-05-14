/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

/**
 * Test LOAD DATA LOCAL INFILE.
 *
 * 1. Create a 50Mb test file
 * 2. Load and read it through MaxScale
 */


#include <maxtest/testconnections.hh>
#include <maxtest/mariadb_connector.hh>
#include <maxbase/stopwatch.hh>
#include <maxbase/string.hh>

using std::string;

const char filename[] = "local_infile.dat";

bool create_datafile(TestConnections& test, size_t datasize);
void test_main(TestConnections& test);
void test_load_data(TestConnections& test, size_t datasize, size_t expected_rows, int wait_limit_s);
bool test_repeated_ldli(TestConnections& test);

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;

    // MXS-4388: Next command hangs after LOAD DATA LOCAL INFILE
    // Quick to test, run it first.
    if (!test_repeated_ldli(test))
    {
        return;
    }

    // This test involves inserting large blocks of data. To speed up the test, use only one slave,
    // as this is not a replication speed test.
    repl.ping_or_open_admin_connections();
    for (int i = 2; i < 4; i++)
    {
        auto admin_conn = repl.backend(i)->admin_connection();
        admin_conn->cmd("stop slave; reset slave all;");
    }
    mxs.wait_for_monitor();
    mxs.check_print_servers_status({mxt::ServerInfo::master_st, mxt::ServerInfo::slave_st,
                                    mxt::ServerInfo::RUNNING, mxt::ServerInfo::RUNNING});

    if (test.ok())
    {
        test_load_data(test, 1000000, 20000, 5);    // 1 MB
        if (test.ok())
        {
            test_load_data(test, 20000000, 500000, 10);     // 20 MB
        }
        if (test.ok())
        {
            // The last load should take 20 seconds. MaxScale is only running 1 routing thread for
            // this test. Check that MaxScale is still responsive for other clients while processing
            // the load.

            std::atomic_bool keep_running {true};
            auto test_func = [&test, &keep_running]() {
                // Sleep a little to ensure LOAD DATA has begun. It takes roughly 2s to generate the
                // 200MB test data array, and 20s to run the LOAD DATA.
                std::this_thread::sleep_for(2s);
                mxb::Duration max_query_time = 0s;
                test.tprintf("Starting queries during LOAD DATA.");

                int i = 0;
                while (keep_running && test.ok())
                {
                    mxb::StopWatch timer;
                    auto test_conn = test.maxscale->open_rwsplit_connection2();
                    auto res = test_conn->simple_query("select rand();");
                    test.expect(!res.empty(), "Query during LOAD DATA failed.");
                    auto dur = timer.split();
                    max_query_time = std::max(max_query_time, dur);
                    std::this_thread::sleep_for(0.2s);
                    i++;
                }

                auto max_dur_s = mxb::to_secs(max_query_time);
                test.tprintf("Queried %i times during LOAD DATA. Max query duration: %f seconds.",
                             i, max_dur_s);
                // The following may need tuning if tester machine network or speed changes significantly.
                // The idea is to detect any big changes in MaxScale behavior.
                test.expect(i > 50 && i < 3000, "Unexpected number of queries: %i.", i);
                test.expect(max_dur_s > 0.001 && max_dur_s < 5, "Unexpected max query duration: %f.",
                            max_dur_s);
            };

            std::thread tester_thread(test_func);
            test_load_data(test, 200000000, 5000000, 60);   // 200 MB
            keep_running = false;
            tester_thread.join();
        }
    }
    mxs.maxctrl("call command mariadbmon reset-replication MariaDB-Monitor server1");
    mxs.sleep_and_wait_for_monitor(1, 1);
    mxs.check_print_servers_status(mxt::ServersInfo::default_repl_states());
}

void test_load_data(TestConnections& test, size_t datasize, size_t expected_rows, int wait_limit_s)
{
    const char table_name[] = "test.dump";
    if (create_datafile(test, datasize))
    {
        auto& mxs = *test.maxscale;
        auto conn = mxs.open_rwsplit_connection2();
        conn->cmd_f("DROP TABLE IF EXISTS %s;", table_name);
        conn->cmd_f("CREATE TABLE %s (a int, b varchar(80), c varchar(80));", table_name);

        if (test.ok())
        {
            test.tprintf("Test table created. Reconnect and load the data to server.");
            auto data_conn = mxs.open_rwsplit_connection2();
            mxb::StopWatch timer;
            data_conn->cmd_f("LOAD DATA LOCAL INFILE '%s' INTO TABLE %s FIELDS TERMINATED BY ',';",
                             filename, table_name);
            if (test.ok())
            {
                test.tprintf("Load data done, waiting for slave sync.");
                auto dur_s = mxb::to_secs(timer.split());
                test.expect(dur_s < wait_limit_s, "LOAD DATA took %f seconds, when less than %d was "
                                                  "expected.", dur_s, wait_limit_s);

                test.repl->sync_slaves(0, wait_limit_s);
                test.tprintf("Slaves synced, check the number of rows in the table.");
                string query = (string)"SELECT count(*) FROM " + table_name;
                auto count_str = data_conn->simple_query(query);
                if (count_str.empty())
                {
                    test.add_failure("Could not read row count.");
                }
                else
                {
                    auto count = std::stol(count_str);
                    test.tprintf("Row count is %li.", count);
                    test.expect(count >= (long)expected_rows,
                                "Only %li columns found, expected at least %zu.", count, expected_rows);
                }
            }
        }
        conn->cmd_f("DROP TABLE %s", table_name);
        test.tprintf("Test table dropped.");
    }
    unlink(filename);
}

bool test_repeated_ldli(TestConnections& test)
{
    if (create_datafile(test, 1024))
    {
        auto conn = test.maxscale->open_rwsplit_connection2();
        const char table_name[] = "test.dump";
        conn->cmd_f("CREATE OR REPLACE TABLE %s (a int, b varchar(80), c varchar(80));", table_name);
        conn->cmd("SET AUTOCOMMIT=0");
        conn->cmd_f("LOAD DATA LOCAL INFILE '%s' INTO TABLE %s", filename, table_name);
        conn->cmd("SET AUTOCOMMIT=1");
        conn->cmd("SET AUTOCOMMIT=0");
        conn->cmd_f("LOAD DATA LOCAL INFILE '%s' INTO TABLE %s", filename, table_name);
        conn->cmd("SET AUTOCOMMIT=1");
        conn->cmd_f("DROP TABLE %s", table_name);
    }

    unlink(filename);
    return test.ok();
}

bool create_datafile(TestConnections& test, size_t datasize)
{
    bool rval = false;
    unlink(filename);
    int fd = open(filename, O_CREAT | O_RDWR | O_EXCL, 0755);
    if (fd >= 0)
    {
        test.tprintf("File '%s' opened. Generating %zu bytes of data.", filename, datasize);
        string data;
        data.reserve(datasize);

        size_t i = 1;
        while (data.length() < datasize)
        {
            char line[128];
            sprintf(line, "%zu,'%zx','%zx'\n", i, i << (10 + i), i << (5 + i));
            data.append(line);
            i++;
        }

        test.tprintf("Data generation complete, writing to file.");
        auto written = write(fd, data.c_str(), data.length());
        close(fd);
        if (written == (long)data.length())
        {
            test.tprintf("Write complete.");
            rval = true;
        }
        else
        {
            test.add_failure("Write failed. Return value %ld. Error %i: %s",
                             written, errno, mxb_strerror(errno));
        }
    }
    else
    {
        test.add_failure("Failed to open file '%s'. Error %i: %s", filename, errno, mxb_strerror(errno));
    }
    return rval;
}
