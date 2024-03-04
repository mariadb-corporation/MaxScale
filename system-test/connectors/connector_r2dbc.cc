/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * Runs the MariaDB Connector/R2DBC test suite against MaxScale
 */
#include "connector_common.hh"

int main(int argc, char** argv)
{
    TestConnections test;
    // Test test appears to take close to 300 seconds to complete. Increase the timeout to make sure
    // it has enough time to complete but not too much to make sure it returns within a reasonable
    // time if it hangs.
    test.reset_timeout(500);

    return run_maven_test(test, argc, argv,
                          "https://github.com/mariadb-corporation/mariadb-connector-r2dbc",
                          "master", "mariadb-connector-r2dbc");
}
