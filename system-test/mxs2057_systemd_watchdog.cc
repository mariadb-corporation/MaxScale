/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/stopwatch.hh>

namespace
{
// watchdog_interval 60 seconds, make sure it is the same in maxscale.service
const maxbase::Duration watchdog_interval = mxb::from_secs(60.0);

// Return true if maxscale stays alive for the duration dur.
bool staying_alive(TestConnections& test, const maxbase::Duration& dur)
{
    bool alive = true;
    maxbase::StopWatch sw_loop_start;
    while (alive && sw_loop_start.split() < dur)
    {
        if (execute_query_silent(test.maxscale->conn_rwsplit, "select 1"))
        {
            alive = false;
            break;
        }
    }

    return alive;
}

// The bulk of the test.
void test_watchdog(TestConnections& test, int argc, char* argv[])
{
    test.log_includes("The systemd watchdog is Enabled");

    // Wait for one watchdog interval, systemd should have been notified in that time.
    staying_alive(test, watchdog_interval);

    test.reset_timeout();

    /**
     * This query will cause catastrophic backtracing with the following pattern:
     *
     *    SELECT.*.*FROM.*.*t1.*.*WHERE.*.*id.*=.*1
     *
     * The worst-case complexity for PCRE2 is exponential and with about 100k characters the time it takes to
     * fail the match is about a minute. This should be long enough to cause the systemd watchdog to kick in
     * when it's repeated five times.
     */
    std::string query = "SELECT id FROM t1 where id = '";

    for (int i = 0; i < 10000; i++)
    {
        query += "x";
    }

    query += "'";

    // Make one thread in maxscale hang
    mysql_query(test.maxscale->conn_rwsplit, query.c_str());

    // maxscale should get killed by systemd in less than duration(interval - epsilon).
    bool maxscale_alive = staying_alive(test, mxb::from_secs(1.2 * mxb::to_secs(watchdog_interval)));

    if (maxscale_alive)
    {
        test.add_result(true, "Although the systemd watchdog is enabled, "
                              "systemd did not terminate maxscale!");
    }
    else
    {
        test.log_includes("received fatal signal 6");
        if (test.global_result == 0)
        {
            test.tprintf("Maxscale was killed by systemd - ok");

            for (int i = 0; i < 30; i++)
            {
                if (test.maxscale->ssh_output("rm /tmp/core*", true).rc == 0)
                {
                    break;
                }
            }
        }
    }
}
}

int main(int argc, char* argv[])
{
    TestConnections test {argc, argv};
    test.maxscale->leak_check(false);
    test.maxscale->connect_rwsplit();

    if (!test.global_result)
    {
        test_watchdog(test, argc, argv);
    }

    return test.global_result;
}
