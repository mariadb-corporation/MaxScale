/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file xpan_mon.cpp - simple Xpand monitor test
 * Just creates Xpand cluster and connect Maxscale to it
 * It can be used as a template for Xpand tests
 *
 * See xpand_nodes.h for details about configiration
 */

#include <maxtest/testconnections.hh>

int main(int argc, char* argv[])
{
    int i;
    TestConnections* Test = new TestConnections(argc, argv);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
