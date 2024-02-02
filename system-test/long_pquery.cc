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
 * @file long_sysbanch.cpp Run 'pquery' for long long execution (long load test)
 *
 */


#include <maxtest/testconnections.hh>
#include <maxtest/galera_cluster.hh>


int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);
    test.galera->start_replication();
    test.install_pquery();

    char sys1[4096];
    sprintf(sys1, "./pquery2-md --address %s --port %d --user %s --password %s "
                  "--threads 1000 --queries-per-thread 1000000 --verbose "
                  "--log-query-duration --log-query-statistics --database test",
            test.galera->ip4(0), test.galera->port[0],
            test.galera->user_name().c_str(), test.galera->password().c_str());
    test.tprintf("%s\n", sys1);

    system(sys1);

    //test.tprintf("Checking if MaxScale is still alive!\n");
    //fflush(stdout);
    //test.check_maxscale_alive();

    int rval = test.global_result;
    return rval;
}
