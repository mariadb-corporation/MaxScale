/*
 * Copyright (c) 2023 MariaDB plc
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
#include <mariadb_rpl.h>

void test_main(TestConnections& test)
{
    if (!test.expect(test.maxscale->connect_rwsplit("") == 0, "Failed to connect"))
    {
        return;
    }

    MYSQL* c = test.maxscale->conn_rwsplit;

    for (auto q : {
        "SET @master_binlog_checksum = @@global.binlog_checksum",
        "SET @mariadb_slave_capability=4",
        "SET @slave_connect_state=''",
        "SET @slave_gtid_strict_mode=1",
        "SET @slave_gtid_ignore_duplicates=1",
        "SET NAMES latin1"})
    {
        test.expect(mysql_query(c, q) == 0, "Query failed: %s", mysql_error(c));
    }

    auto rpl = mariadb_rpl_init(c);

    if (test.expect(rpl, "Failed to create replication handle: %s", mysql_error(c)))
    {
        unsigned int server_id = 123456;
        mariadb_rpl_optionsv(rpl, MARIADB_RPL_SERVER_ID, server_id);
        mariadb_rpl_optionsv(rpl, MARIADB_RPL_START, 4);
        mariadb_rpl_optionsv(rpl, MARIADB_RPL_FLAGS, MARIADB_RPL_BINLOG_SEND_ANNOTATE_ROWS);

        test.expect(mariadb_rpl_open(rpl) == 0, "Failed to start replication: %s", mysql_error(c));

        auto ev = mariadb_rpl_fetch(rpl, nullptr);
        test.expect(!ev, "No event should be sent");

        test.expect(mysql_errno(c) == 1289,
                    "MaxScale should respond with ER_FEATURE_DISABLED, got %d", mysql_errno(c));
        test.expect(mysql_error(c) == "Replication protocol is disabled"s,
                    "MaxScale responded with wrong message: %s", mysql_error(c));
        mariadb_free_rpl_event(ev);
        mariadb_rpl_close(rpl);
    }
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
