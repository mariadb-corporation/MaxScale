/*
 * Copyright (c) 2025 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-03-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * MXS-5302: Executing more than max_sescmd_history prepared statement loses some of them
 * https://jira.mariadb.org/browse/MXS-5302
 */

#include <maxtest/testconnections.hh>
#include <maxbase/string.hh>
#include <sstream>


void test_main(TestConnections& test)
{
    // Stop replication on all servers
    test.check_maxctrl("unlink service RW-Split-Router server2 server3 server4");
    test.repl->execute_query_all_nodes("STOP SLAVE;SET GLOBAL rpl_semi_sync_slave_enabled=1;");

    // Start replication on server2 and make it go through readwritesplit
    std::ostringstream ss;
    ss << "STOP SLAVE;"
       << "CHANGE MASTER TO MASTER_PORT=" << test.maxscale->port(mxt::MaxScale::RWSPLIT) << ","
       << "  MASTER_HOST='" << test.maxscale->ip() << "';"
       << "START SLAVE;";

    auto replica = test.repl->get_connection(1);
    replica.connect();
    replica.query(ss.str());

    auto primary = test.repl->get_connection(0);
    primary.connect();
    primary.query("SET GLOBAL rpl_semi_sync_master_enabled=1,rpl_semi_sync_master_timeout=DEFAULT");
    primary.query("CREATE OR REPLACE TABLE test.t1(id INT)");

    for (int i = 0; i < 10; i++)
    {
        primary.query("INSERT INTO test.t1 VALUES (" + std::to_string(i) + ")");
    }

    auto semi_sync = primary.field("SHOW STATUS LIKE 'Rpl_semi_sync_master_status'", 1);
    test.tprintf("Semi-sync: %s", semi_sync.c_str());
    test.expect(mxb::upper_case_copy(semi_sync) == "ON", "Semi-sync is not enabled");

    ss.str("");
    ss << "STOP SLAVE;"
       << "CHANGE MASTER TO MASTER_PORT=" << test.repl->port(0) << ","
       << "  MASTER_HOST='" << test.repl->ip(0) << "';"
       << "START SLAVE;";

    replica.query(ss.str());
    test.repl->execute_query_all_nodes("SET GLOBAL rpl_semi_sync_slave_enabled=0;");
    primary.query("SET GLOBAL rpl_semi_sync_master_enabled=0");
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
