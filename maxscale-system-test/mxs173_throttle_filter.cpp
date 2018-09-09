/*
 * MXS-173 throttling filter
 *
 */

#include "testconnections.h"
#include <base/stopwatch.h>
#include <base/appexception.h>
#include <iostream>
#include <memory>
#include <sstream>
#include <cmath>

DEFINE_EXCEPTION(Whoopsy);

// TODO these should be read from maxscale.cnf. Maybe the test-lib should replace
// any "###ENV_VAR###", with environment variables so that code and conf can share.
constexpr int max_qps = 1000;
constexpr float throttling_duration = 10000 / 1000.0;
constexpr float sampling_duration = 250 / 1000.0;
constexpr float continuous_duration = 2000 / 1000.0;

constexpr int NUM_ROWS = 100000;

void create_table(MYSQL* conn)
{
    if (execute_query_silent(conn,
                             "drop table if exists test.throttle;"
                             "create table test.throttle(id int, name varchar(30),"
                             "primary key(id));"))
    {
        THROW(Whoopsy, "Create table failed - could not start test");
    }
}

void insert_rows(MYSQL* conn)
{
    std::ostringstream os;
    os << "insert into throttle values\n";

    for (int i = 0; i < NUM_ROWS; ++i)
    {
        if (i != 0)
        {
            os << ',';
        }
        os << '(' << i << ", '"
           << std::to_string(i) << "')\n";
    }

    os << ';';

    if (execute_query_silent(conn, os.str().c_str()), false)
    {
        THROW(Whoopsy, "Inserts failed - could not start test");
    }
}

struct ReadSpeed
{
    bool           error;
    base::Duration duration;
    float          qps;
};

ReadSpeed read_rows(MYSQL* conn, int num_rows, int max_qps, bool expect_error)
{
    base::StopWatch sw;
    for (int i = 0; i < num_rows; ++i)
    {
        int index = rand() % NUM_ROWS;
        std::ostringstream os;
        os << "select name from test.throttle where id=" << index;

        if (mysql_query(conn, os.str().c_str()))
        {
            if (expect_error)
            {
                base::Duration dur = sw.lap();
                return {true, dur, i / std::chrono::duration<float>(dur).count()};
            }
            else
            {
                THROW(Whoopsy, "Unexpected error while reading rows.");
            }
        }

        // Maybe a create function template make_unique_deleter. Should work for most cases.
        // But it would be safer with one regular function per type/function pair, returning
        // a unique pointer.
        std::unique_ptr<MYSQL_RES, void (*)(MYSQL_RES*)> result(mysql_store_result(conn),
                                                                mysql_free_result);
        if (!result)
        {
            THROW(Whoopsy, "No resultset for index=" << index);
        }

        MYSQL_ROW row = mysql_fetch_row(&*result);
        if (row)
        {
            if (std::stoi(row[0]) != index)
            {
                THROW(Whoopsy, "Differing values index=" << index << " name=" << row[0]);
            }
        }
        else
        {
            THROW(Whoopsy, "Row id = " << index << " not in resultset.");
        }


        if ((row = mysql_fetch_row(&*result)))
        {
            THROW(Whoopsy, "Extra row index = " << index << " name = " << row[0] << " in resultset.");
        }
    }

    base::Duration dur = sw.lap();
    return ReadSpeed {false, dur, num_rows / std::chrono::duration<float>(dur).count()};
}

void gauge_raw_speed(TestConnections& test)
{
    const int raw_rows = NUM_ROWS / 5;
    std::cout << "\n****\nRead " << raw_rows
              << " rows via master readconnrouter, to gauge speed.\n";
    auto rs = read_rows(test.maxscales->conn_master[0], raw_rows, 0, false);
    std::cout << rs.qps << "qps " << " duration " << rs.duration << '\n';

    if (rs.qps < 2 * max_qps)
    {
        std::ostringstream os;
        os << "The raw speed is too slow, " << rs.qps
           << "qps, compared to max_qps = " << max_qps << "qps for accurate testing.";
        test.add_result(1, os.str().c_str());
    }
}

void verify_throttling_performace(TestConnections& test)
{
    int three_quarter = 3 * max_qps * throttling_duration / 4;
    std::cout << "\n****\nRead " << three_quarter
              << " rows which should take about " << 3 * throttling_duration / 4
              << " seconds.\nThrottling should keep qps around "
              << max_qps << ".\n";
    auto rs1 = read_rows(test.maxscales->conn_rwsplit[0], three_quarter, 0, false);
    std::cout << "1: " << rs1.qps << "qps " << " duration " << rs1.duration << '\n';
    std::cout << "Sleep for " << continuous_duration << "s (continuous_duration)\n";
    usleep(continuous_duration * 1000000);
    std::cout << "Run the same read again. Should be throttled, but not disconnected.\n";
    auto rs2 = read_rows(test.maxscales->conn_rwsplit[0], three_quarter, 0, false);
    std::cout << "2: " << rs2.qps << "qps " << " duration " << rs2.duration << '\n';

    if (std::abs(rs1.qps - max_qps) > 0.1 * max_qps
        || std::abs(rs2.qps - max_qps) > 0.1 * max_qps)
    {
        std::ostringstream os;
        os << "Throttled speed 1: " << rs1.qps << " or 2: " << rs2.qps
           << "differs from max_qps " << max_qps << " by more than 10%%";
        test.add_result(1, os.str().c_str());
    }
}

void verify_throttling_disconnect(TestConnections& test)
{
    int half_rows = max_qps * throttling_duration / 2;
    std::cout << "\n****\nRead " << 3 * half_rows
              << " rows which should cause a disconnect at a little\nbelow "
              << half_rows << " rows to go, in about " << throttling_duration << "s.\n";
    auto rs = read_rows(test.maxscales->conn_rwsplit[0], 3 * half_rows, 0, true);
    std::cout << rs.qps << "qps " << " duration " << rs.duration << '\n';

    if (!rs.error)
    {
        std::ostringstream os;
        os << "Throttle filter did not disconnect rogue session.\n"
           << rs.qps << "qps " << " duration " << rs.duration;
        test.add_result(1, os.str().c_str());
    }
    if (std::abs(rs.qps - max_qps) > 0.1 * max_qps)
    {
        std::ostringstream os;
        os << "Throttled speed " << rs.qps << " differs from max_qps " << max_qps
           << " by more than 10%%";
        test.add_result(1, os.str().c_str());
    }
}

int main(int argc, char* argv[])
{
    srand(clock());
    TestConnections test {argc, argv};

    try
    {
        test.maxscales->connect_maxscale(0);

        std::cout << "Create table\n";
        test.set_timeout(120);
        create_table(test.maxscales->conn_master[0]);

        std::cout << "Insert rows\n";
        test.set_timeout(120);
        insert_rows(test.maxscales->conn_master[0]);

        test.set_timeout(120);
        gauge_raw_speed(test);

        test.stop_timeout();
        test.repl->sync_slaves();

        test.set_timeout(120);
        verify_throttling_performace(test);

        test.maxscales->close_maxscale_connections(0);
        test.maxscales->connect_maxscale(0);

        test.set_timeout(120);
        verify_throttling_disconnect(test);

        std::cout << "\n\n";
    }
    catch (std::exception& ex)
    {
        test.add_result(1, ex.what());
    }
    catch (...)
    {
        test.add_result(1, "catch ...");
        throw;
    }

    test.repl->connect();
    execute_query(test.repl->nodes[0], "DROP TABLE test.throttle");
    test.repl->disconnect();

    return test.global_result;
}
