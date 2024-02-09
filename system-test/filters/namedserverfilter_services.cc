/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.repl->connect("mysql");
    auto expected = test.repl->get_all_server_ids_str();

    auto c = test.maxscale->get_connection(4006);
    test.expect(c.connect(), "Failed to connect: %s", c.error());
    std::vector<std::string> ids;
    ids.push_back(c.field("SELECT @@server_id, 'RCR1'"));
    ids.push_back(c.field("SELECT @@server_id, 'RCR2'"));
    ids.push_back(c.field("SELECT @@server_id, 'RCR3'"));
    ids.push_back(c.field("SELECT @@server_id, 'RCR4'"));

    test.expect(ids == expected,
                "Expected '%s', got '%s'", mxb::join(expected, ", ").c_str(), mxb::join(ids, ", ").c_str());

    return test.global_result;
}
