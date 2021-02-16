/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// To ensure that ss_info_assert asserts also when builing in non-debug mode.
#if !defined (SS_DEBUG)
#define SS_DEBUG
#endif
#if defined (NDEBUG)
#undef NDEBUG
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <maxscale/config.hh>
#include <maxscale/listener.hh>
#include <maxscale/routingworker.hh>

#include "../dcb.cc"
#include "test_utils.hh"

/**
 * test1    Allocate a dcb and do lots of other things
 *
 */
static int test1()
{
    DCB* dcb;
    /* Single buffer tests */
    fprintf(stderr, "testdcb : creating buffer with type DCB::Role::INTERNAL");
    MXS_CONFIG_PARAMETER parameters;
    parameters.set("max_retry_interval", "10s");
    parameters.set("connection_timeout", "10s");
    parameters.set("net_write_timeout", "10s");
    auto service = service_alloc("service", "readconnroute", &parameters);

    MXS_CONFIG_PARAMETER listener_params;
    listener_params.set(CN_ADDRESS, "0.0.0.0");
    listener_params.set(CN_PORT, "3306");
    listener_params.set(CN_PROTOCOL, "mariadbclient");
    listener_params.set(CN_SERVICE, service->name());

    auto listener = Listener::create("listener", "mariadbclient", listener_params);

    auto session = new mxs::Session(listener);
    dcb = dcb_alloc(DCB::Role::INTERNAL, session);
    printDCB(dcb);
    fprintf(stderr, "\t..done\nAllocated dcb.");
    // TODO: Without running workers, the following will hang. As it does not
    // TODO: really add value (the only created dcb is the one above), we'll
    // TODO: exclude it.
    // TODO: Some kind of test environment with workers would be needed.
    // printAllDCBs();
    fprintf(stderr, "\t..done\n");
    dcb->state = DCB_STATE_POLLING;
    this_thread.current_dcb = dcb;
    dcb_close(dcb);
    fprintf(stderr, "Freed original dcb");
    fprintf(stderr, "\t..done\n");

    return 0;
}

int main(int argc, char** argv)
{
    int result = 0;

    init_test_env(NULL);

    result += test1();

    exit(result);
}
