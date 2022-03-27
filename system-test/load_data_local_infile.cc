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

void create_datafile(TestConnections& test, size_t datasize);
void test_main(TestConnections& test);

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    create_datafile(test, 1024 * 1024 * 50);

    const char table_name[] = "test.dump";
    const char set_max_packet[] = "set global max_allowed_packet=(%s)";
    auto& mxs = *test.maxscale;
    auto conn = mxs.open_rwsplit_connection2();

    // Modifying a global variable. Backup its value.
    string max_packet_old = conn->simple_query("select @@global.max_allowed_packet");
    if (test.ok())
    {
        test.tprintf("Set max packet size and create a test table.");
        conn->cmd_f(set_max_packet, "1048576 * 60");
        conn->cmd_f("DROP TABLE IF EXISTS %s", table_name);
        conn->cmd_f("CREATE TABLE %s (a int, b varchar(80), c varchar(80))", table_name);

        if (test.ok())
        {
            test.tprintf("Reconnect and load the data to server.");
            auto data_conn = mxs.open_rwsplit_connection2();
            data_conn->cmd_f("LOAD DATA LOCAL INFILE '%s' INTO TABLE %s FIELDS TERMINATED BY ','",
                             filename, table_name);
            if (test.ok())
            {
                test.tprintf("Load data done, waiting for slave sync.");
                test.repl->sync_slaves();
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
                    test.expect(count > 1000000, "Only %li columns found, expected more.", count);
                }
            }
        }
        conn->cmd_f("DROP TABLE %s", table_name);
        conn->cmd_f(set_max_packet, max_packet_old.c_str());
    }

    unlink(filename);
}

void create_datafile(TestConnections& test, size_t datasize)
{
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
        }
        else
        {
            test.add_failure("Write failed. Return value %ld. Error %i: %s",
                             written, errno, mxb_strerror(errno));
        }
    }
    else
    {
        test.tprintf("Failed to open file '%s'. Error %i: %s", filename, errno, mxb_strerror(errno));
    }
}
