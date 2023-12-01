/*
 * Copyright (c) 2022 MariaDB Corporation Ab
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
 * Runs the MariaDB Connector/J test suite against MaxScale
 */
#include "connector_common.hh"

int main(int argc, char** argv)
{
    TestConnections test;
    // The Connector/J test also takes while, give it some extra time to complete.
    test.reset_timeout(500);

    return run_maven_test(test, argc, argv,
                          "https://github.com/mariadb-corporation/mariadb-connector-j",
                          "master", "mariadb-connector-j");
}
