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

/**
 * MXS-1831: No error on invalid monitor parameter alteration
 *
 * https://jira.mariadb.org/browse/MXS-1831
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto rv = test.maxctrl("alter monitor MySQL-Monitor not_a_parameter=not_a_value");
    test.expect(rv.rc != 0, "Altering unknown parameter should cause an error: %s", rv.output.c_str());
    rv = test.maxctrl("alter monitor MySQL-Monitor auto_rejoin=on_sunday_afternoons");
    test.expect(rv.rc != 0, "Invalid parameter value should cause an error: %s", rv.output.c_str());

    return test.global_result;
}
