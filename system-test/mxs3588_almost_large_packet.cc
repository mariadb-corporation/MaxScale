/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    std::string query = "SELECT '";

    // One byte command byte and one byte for the single quote
    query.append(0xfffffb - 1 - query.size() - 1, 'a');
    query += "'";

    auto c = test.maxscale->rwsplit();
    c.connect();
    test.expect(c.query(query), "First query should work: %s", c.error());
    test.expect(c.query(query), "Second query should work: %s", c.error());
    test.expect(c.query(query), "Third query should work: %s", c.error());
    test.expect(c.query("SELECT 1"), "Small query should work: %s", c.error());

    return test.global_result;
}
