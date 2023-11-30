/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * MXS-2350: On-demand connection creation
 * https://jira.mariadb.org/browse/MXS-2350
 */

#include <maxtest/testconnections.hh>
#include <maxbase/string.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    Connection c = test.maxscale->rwsplit();

    test.expect(c.connect(), "Connection should work");
    auto output = test.maxscale->ssh_output("maxctrl list servers --tsv|cut -f 4|sort|uniq").output;
    mxb::trim(output);
    test.expect(output == "0", "Servers should have no connections: %s", output.c_str());
    c.disconnect();

    test.expect(c.connect(), "Connection should work");
    test.expect(c.query("SELECT 1"), "Read should work");
    c.disconnect();

    test.expect(c.connect(), "Connection should work");
    test.expect(c.query("SELECT @@last_insert_id"), "Write should work");
    c.disconnect();

    test.expect(c.connect(), "Connection should work");
    test.expect(c.query("SET @a = 1"), "Session command should work");
    c.disconnect();

    test.expect(c.connect(), "Connection should work");
    test.expect(c.query("BEGIN"), "BEGIN should work");
    test.expect(c.query("SELECT 1"), "Read should work");
    test.expect(c.query("COMMIT"), "COMMIT command should work");
    c.disconnect();

    test.expect(c.connect(), "Connection should work");
    test.expect(c.query("SET @a = 1"), "Session command should work");

    test.repl->block_all_nodes();
    test.maxscale->wait_for_monitor();
    test.repl->unblock_all_nodes();
    test.maxscale->wait_for_monitor();

    test.expect(c.query("SET @a = 1"), "Session command should work: %s", c.error());
    c.disconnect();

    return test.global_result;
}
