/**
 * MXS-1926: LOAD DATA LOCAL INFILE interrupted by server shutdown
 *
 * https://jira.mariadb.org/browse/MXS-1926
 */

#include "testconnections.h"
#include <stdlib.h>
#include <thread>
#include <fstream>
#include <chrono>
#include <atomic>

using namespace std::chrono;

typedef high_resolution_clock Clock;

std::atomic<int> ROWCOUNT{10000};

std::string create_tmpfile()
{
    char filename[] = "/tmp/data.csv.XXXXXX";
    int fd = mkstemp(filename);
    std::ofstream file(filename);
    close(fd);

    for (int i = 0; i < ROWCOUNT; i++)
    {
        file << "1, 2, 3, 4\n";
    }

    return filename;
}

void tune_rowcount(TestConnections& test)
{
    milliseconds dur{1};
    test.tprintf("Tuning data size so that an insert takes 10 seconds");
    test.maxscales->connect();
    test.try_query(test.maxscales->conn_rwsplit[0], "SET sql_log_bin=0");

    while (dur < seconds(10))
    {
        std::string filename = create_tmpfile();

        auto start = Clock::now();
        test.try_query(test.maxscales->conn_rwsplit[0], "LOAD DATA LOCAL INFILE '%s' INTO TABLE test.t1",
                       filename.c_str());
        auto end = Clock::now();
        dur = duration_cast<milliseconds>(end - start);
        test.try_query(test.maxscales->conn_rwsplit[0], "TRUNCATE TABLE test.t1");

        remove(filename.c_str());

        int orig = ROWCOUNT;
        ROWCOUNT = orig / dur.count() * 10000;
        test.tprintf("Loading %d rows took %d ms, setting row count to %d",
                     orig, dur.count(), ROWCOUNT.load());
    }

    test.maxscales->disconnect();
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.repl->connect();

    // Create the table
    execute_query(test.repl->nodes[0], "CREATE OR REPLACE TABLE test.t1 (a INT, b INT, c INT, d INT)");
    test.repl->sync_slaves();

    // Tune the amount of data so that the loading will take approximately 15 seconds
    tune_rowcount(test);

    std::string filename = create_tmpfile();

    // Connect to MaxScale and load enough data so that we have
    test.maxscales->connect();

    // Disable replication of the LOAD DATA LOCAL INFILE
    test.try_query(test.maxscales->conn_rwsplit[0], "SET sql_log_bin=0");

    test.tprintf("Loading %d rows of data while stopping a slave", ROWCOUNT.load());
    std::thread thr([&]()
    {
        std::this_thread::sleep_for(milliseconds(10));
        test.repl->stop_node(3);
        test.repl->start_node(3);
    });
    test.try_query(test.maxscales->conn_rwsplit[0], "LOAD DATA LOCAL INFILE '%s' INTO TABLE test.t1",
                   filename.c_str());
    test.tprintf("Load complete");
    thr.join();

    test.maxscales->disconnect();

    // Cleanup
    execute_query(test.repl->nodes[0], "DROP TABLE test.t1");
    test.repl->sync_slaves();
    test.repl->disconnect();

    remove(filename.c_str());
    return test.global_result;
}
