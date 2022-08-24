/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-08-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "mariadbmon_utils.hh"

namespace
{
enum class SyncMxs
{
    YES,
    NO
};

bool generate_traffic_and_check(TestConnections& test, mxt::MariaDB* conn, int insert_count, SyncMxs sync);
}

/**
 * Do inserts, check that results are as expected.
 *
 * @param test Test connections
 * @param conn Which specific connection to use
 * @param insert_count How many inserts should be done
 * @return True, if successful
 */
bool generate_traffic_and_check(TestConnections& test, mxt::MariaDB* conn, int insert_count)
{
    return generate_traffic_and_check(test, conn, insert_count, SyncMxs::YES);
}

bool generate_traffic_and_check_nosync(TestConnections& test, mxt::MariaDB* conn, int insert_count)
{
    return generate_traffic_and_check(test, conn, insert_count, SyncMxs::NO);
}

namespace
{
bool generate_traffic_and_check(TestConnections& test, mxt::MariaDB* conn, int insert_count, SyncMxs sync)
{
    const bool wait_sync = (sync == SyncMxs::YES);
    const char table[] = "test.t1";
    int inserts_start = 1;

    auto show_tables = conn->query("show tables from test like 't1';");
    if (show_tables && show_tables->next_row() && show_tables->get_string(0) == "t1")
    {
        auto res = conn->query_f("select count(*) from %s;", table);
        if (res && res->next_row())
        {
            inserts_start = res->get_int(0) + 1;
        }
    }
    else if (test.ok())
    {
        conn->cmd_f("create table %s(c1 int)", table);
    }

    bool ok = false;
    if (test.ok())
    {
        int inserts_end = inserts_start + insert_count;

        ok = true;
        for (int i = inserts_start; i <= inserts_end && ok; i++)
        {
            ok = conn->cmd_f("insert into %s values (%d);", table, i);
        }

        if (ok)
        {
            if (wait_sync)
            {
                test.sync_repl_slaves();
            }

            auto res = conn->query_f("SELECT * FROM %s;", table);
            if (res)
            {
                // Check all values, they should go from 1 to inserts_end
                int expected_val = 0;
                while (res->next_row() && ok)
                {
                    expected_val++;
                    auto value = res->get_int(0);
                    if (value != expected_val)
                    {
                        test.add_failure("Query returned %ld when %d was expected.", value, expected_val);
                        ok = false;
                    }
                }

                if (ok && expected_val != inserts_end)
                {
                    test.add_failure("Query returned %d rows when %d rows were expected.",
                                     expected_val, insert_count);
                    ok = false;
                }

                if (ok && wait_sync)
                {
                    // Wait for monitor to detect gtid change.
                    test.maxscale->wait_for_monitor();
                }
            }
            else
            {
                ok = false;
            }
        }
    }
    return ok;
}
}
