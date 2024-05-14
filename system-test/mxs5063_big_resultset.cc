/*
 * Copyright (c) 2024 MariaDB plc
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

void test_main(TestConnections& test)
{
    // This should be about a 1TiB of data per client. With 10 clients that should be enough to cause an OOM.
    size_t lots_of_rows = 1024UL * 1024UL * 1024UL;
    const std::string sql = "SELECT REPEAT('a', 1024) FROM seq_0_to_" + std::to_string(lots_of_rows);
    std::vector<MYSQL*> conns;
    std::vector<MYSQL_RES*> results;

    for (int i = 0; i < 10; i++)
    {
        MYSQL* c = test.maxscale->open_rwsplit_connection();

        bool ok = c
            && mysql_send_query(c, sql.c_str(), sql.size()) == 0
            && mysql_read_query_result(c) == 0;

        if (test.expect(ok, "Failed to connect and query: %s", c ? mysql_error(c) : "No connection"))
        {
            results.push_back(mysql_use_result(c));
            conns.push_back(c);
        }
        else
        {
            mysql_close(c);
            break;
        }
    }

    int prev_mem = 0;
    int stable_loops = 0;

    while (stable_loops < 10 && test.ok())
    {
        for (auto* res : results)
        {
            auto row = mysql_fetch_row(res);

            if (!test.expect(row, "Expected at least one row to be available"))
            {
                break;
            }
        }

        std::string status = test.maxscale->ssh_output("ps -C maxscale -o %mem=,%cpu=").output;
        test.tprintf("MEM%% and CPU%%: %s", status.c_str());

        // This has the effect of rounding the memory usage to whole percentages.
        int mem = atoi(status.c_str());

        if (mem == prev_mem)
        {
            ++stable_loops;
        }
        else if (mem - prev_mem < -50)
        {
            test.add_failure("Over 50%% drop in memory usage: %d%%", mem - prev_mem);
            break;
        }
        else
        {
            stable_loops = 0;
        }

        prev_mem = mem;
        sleep(1);
    }

    if (stable_loops == 10)
    {
        test.tprintf("Memory usage is stable");
    }

    std::for_each(conns.begin(), conns.end(), mariadb_cancel);
    std::for_each(results.begin(), results.end(), mysql_free_result);
    std::for_each(conns.begin(), conns.end(), mysql_close);
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
