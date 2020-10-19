/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/mariadb_func.hh>
#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    Connection rwsplit = test.maxscales->rwsplit();

    test.expect(rwsplit.connect(),
                "Could not connect to rwsplit.");
    test.expect(rwsplit.query("CREATE TEMPORARY TABLE mxs2250 (a int)"),
                "Could not create temporary table.");
    test.expect(rwsplit.query("DESCRIBE mxs2250"),
                "Could not describe temporary table.");

    return test.global_result;
}
