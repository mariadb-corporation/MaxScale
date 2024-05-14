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

/**
 * MXS-2115: Automatic version string detection doesn't work
 *
 * When servers are available, the backend server and Maxscale should return the
 * same version string.
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    test.repl->connect();
    test.maxscale->connect();

    std::string direct = mysql_get_server_info(test.repl->nodes[0]);
    std::string mxs = mysql_get_server_info(test.maxscale->conn_rwsplit);
    test.expect(direct == mxs, "MaxScale sends wrong version: %s != %s", direct.c_str(), mxs.c_str());

    return test.global_result;
}
