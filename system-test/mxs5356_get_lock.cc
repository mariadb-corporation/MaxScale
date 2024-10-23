/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>

void test_main(TestConnections& test)
{
    const std::string USER = "mxs5356_get_lock";
    const std::string PASSWORD = "mxs5356_get_lock";

    auto r = test.repl->get_connection(0);
    r.connect();
    r.query("CREATE USER " + USER + " IDENTIFIED BY '" + PASSWORD + "'");
    r.query("GRANT ALL ON *.* TO " + USER);
    test.repl->sync_slaves();

    auto c = test.maxscale->rwsplit();
    c.set_credentials(USER, PASSWORD);
    MXT_EXPECT(c.connect());
    MXT_EXPECT(c.query("SELECT GET_LOCK('mxs5356_get_lock', 1)"));
    test.repl->execute_query_all_nodes(("KILL USER " + USER).c_str());
    MXT_EXPECT(!c.query("SELECT @@server_id"));
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
