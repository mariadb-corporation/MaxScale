/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

/**
 * MXS-1889: generated [maxscale] section causes errors
 *
 * https://jira.mariadb.org/browse/MXS-1899
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.maxscale->ssh_node_f(true, "maxctrl alter maxscale auth_connect_timeout 10s");
    test.expect(test.maxscale->restart() == 0,
                "Restarting MaxScale after modification "
                "of global parameters should work");

    return test.global_result;
}
