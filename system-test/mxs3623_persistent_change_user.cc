/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto c = test.maxscale->rwsplit();
    c.connect();
    c.query("CREATE USER test IDENTIFIED BY 'test'");
    c.query("GRANT ALL ON *.* TO test");
    test.repl->sync_slaves();

    test.expect(c.connect(), "Failed to create connection: %s", c.error());
    auto id = c.field("SELECT CONNECTION_ID()");
    c.disconnect();

    test.tprintf("Connection ID before test: %s", id.c_str());

    for (int i = 0; i < 100 && test.ok(); i++)
    {
        test.expect(c.connect(), "Failed to create connection: %s", c.error());
        test.expect(c.change_user("test", "test"), "Failed to change user: %s", c.error());
        auto current_id = c.field("SELECT CONNECTION_ID()");
        test.expect(current_id == id,
                    "Expected connection ID to be %s, not %s", id.c_str(), current_id.c_str());
        c.disconnect();
    }

    test.expect(c.connect(), "Failed to create connection: %s", c.error());
    id = c.field("SELECT CONNECTION_ID()");
    c.disconnect();

    test.tprintf("Connection ID after test: %s", id.c_str());


    c.connect();
    c.query("DROP USER test");

    return test.global_result;
}
