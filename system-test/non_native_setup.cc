/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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
 * @file Simple dummy configuration program for non-C++ tests
 * - Configure Maxscale (prepare maxscale.cnf and copy it to Maxscale machine)
 * - check backends
 * - try to restore broken backends
 */

#include <iostream>
#include <stdlib.h>
#include <maxbase/format.hh>
#include <maxtest/testconnections.hh>

using namespace std;

namespace
{

void print_usage(const char* zProgram)
{
    cout << "usage: " << zProgram << "[<flags>] <name> <script>\n"
         << "\n"
         << "where\n"
         << "  <flags>   are flags that will be provided to the TestConnections contructor,\n"
         << "  <name>    is the name of the test, and\n"
         << "  <script>  is the program that will be invoked." << endl;
}
}

int main(int argc, char* argv[])
{
    const char* zScript = nullptr;
    const char* zName = nullptr;

    for (int i = 1; i < argc; ++i)
    {
        if (*argv[i] != '-')
        {
            zName = argv[i];

            if (i + 1 < argc)
            {
                zScript = argv[i + 1];
            }

            break;
        }
    }

    int rv = 1;

    if (zName && zScript)
    {
        TestConnections test(argc, argv);
        rv = test.run_test_script(zScript, zName);
    }
    else
    {
        print_usage(argv[0]);
    }

    return rv;
}
