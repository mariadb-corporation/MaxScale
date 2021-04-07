/**
 * @file avro.cpp test of avro
 * - setup binlog and avro
 * - put some data to t1
 * - check avro file with "maxavrocheck -vv /var/lib/maxscale/avro/test.t1.000001.avro"
 * - check that data in avro file is correct
 */

#include <sstream>
#include <maxtest/cdc_tools.hh>
#include <maxtest/sql_t1.hh>
#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    test.set_timeout(120);
    test.repl->connect();

    // This makes sure the binlogs don't have anything else
    execute_query(test.repl->nodes[0], "RESET MASTER");

    // MXS-2095: Crash on GRANT CREATE TABLE
    execute_query(test.repl->nodes[0], "CREATE USER test IDENTIFIED BY 'test'");
    execute_query(test.repl->nodes[0], "GRANT CREATE TEMPORARY TABLES ON *.* TO test");
    execute_query(test.repl->nodes[0], "DROP USER test");

    create_t1(test.repl->nodes[0]);
    insert_into_t1(test.repl->nodes[0], 3);
    execute_query(test.repl->nodes[0], "FLUSH LOGS");

    test.repl->close_connections();
    test.maxscales->start();

    /** Give avrorouter some time to process the events */
    test.stop_timeout();
    sleep(10);
    test.set_timeout(120);

    auto res = test.maxscales->ssh_output("maxavrocheck -d /var/lib/maxscale/avro/test.t1.000001.avro");

    std::istringstream iss;
    iss.str(res.output);
    int x1_exp = 0;
    int fl_exp = 0;
    int x = 16;

    for (std::string line; std::getline(iss, line);)
    {
        long long int x1, fl;
        test.set_timeout(20);
        mxt::get_x_fl_from_json(line.c_str(), &x1, &fl);

        if (x1 != x1_exp || fl != fl_exp)
        {
            test.add_result(1, "Output:x1 %lld, fl %lld, Expected: x1 %d, fl %d",
                            x1, fl, x1_exp, fl_exp);
            break;
        }

        if ((++x1_exp) >= x)
        {
            x1_exp = 0;
            x = x * 16;
            fl_exp++;
            test.tprintf("fl = %d", fl_exp);
        }
    }

    if (fl_exp != 3)
    {
        test.add_result(1, "not enough lines in avrocheck output");
    }

    execute_query(test.repl->nodes[0], "DROP TABLE test.t1");
    test.stop_timeout();

    return test.global_result;
}
