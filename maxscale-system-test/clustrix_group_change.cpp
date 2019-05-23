/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include "testconnections.h"
#include "maxrest.hh"

using namespace std;

namespace
{

const set<string> bootstrap_servers =
{
    "clustrix_server1",
    "clustrix_server2",
    "clustrix_server3",
    "clustrix_server4",
};

void run_test(TestConnections& test)
{
    MaxRest maxrest(&test);

    Maxscales* pMaxscales = test.maxscales;
    test.add_result(pMaxscales->connect_rwsplit(), "Could not connect to RWS.");
}

}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    try
    {
        run_test(test);
    }
    catch (const std::exception& x)
    {
        cout << "Exception: " << x.what() << endl;
    }

    return test.global_result;
}
