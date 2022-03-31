/**
 * Test LOAD DATA LOCAL INFILE.
 *
 * 1. Create a 50Mb test file
 * 2. Load and read it through MaxScale
 */


#include <maxtest/testconnections.hh>
#include <maxtest/mariadb_connector.hh>

using std::string;

const char filename[] = "local_infile.dat";

bool create_datafile(TestConnections& test, size_t datasize);
void test_main(TestConnections& test);
void test_load_data(TestConnections& test, size_t datasize, size_t expected_rows, int wait_limit_s);

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    auto& mxs = *test.maxscale;
    auto& repl = *test.repl;

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
            test_load_data(test, 200000000, 5000000, 60);   // 200 MB
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

            data_conn->cmd_f("LOAD DATA LOCAL INFILE '%s' INTO TABLE %s FIELDS TERMINATED BY ',';",
                             filename, table_name);
            if (test.ok())
            {
                test.tprintf("Load data done, waiting for slave sync.");
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
                    test.tprintf("Select returned %li rows.", count);
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
