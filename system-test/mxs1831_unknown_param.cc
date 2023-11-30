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
 * MXS-1831: No error on invalid monitor parameter alteration
 *
 * https://jira.mariadb.org/browse/MXS-1831
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    int rc = test.maxscale->ssh_node_f(true,
                                       "maxctrl alter monitor MySQL-Monitor not_a_parameter not_a_value|grep Error");
    test.expect(rc == 0, "Altering unknown parameter should cause an error");
    rc = test.maxscale->ssh_node_f(true,
                                   "maxctrl alter monitor MySQL-Monitor ignore_external_masters on_sunday_afternoons|grep Error");
    test.expect(rc == 0, "Invalid parameter value should cause an error");

    return test.global_result;
}
