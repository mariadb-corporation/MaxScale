/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
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
#include <errno.h>
#include <maxscale/authenticator2.hh>
#include <maxscale/dcb.hh>
#include <maxscale/listener.hh>
#include <maxscale/service.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

#include "test_utils.hh"
#include "../internal/service.hh"
#include "../internal/session.hh"

/**
 * test1    Allocate a service and do lots of other things
 *
 */

static void test1()
{
    /* Poll tests */
    fprintf(stderr, "Add a DCB");
    MXS_CONFIG_PARAMETER parameters;
    parameters.set(CN_MAX_RETRY_INTERVAL, "10s");
    parameters.set(CN_CONNECTION_TIMEOUT, "10s");
    parameters.set(CN_NET_WRITE_TIMEOUT, "10s");
    auto service = service_alloc("service", "readconnroute", &parameters);

    MXS_CONFIG_PARAMETER listener_params;
    listener_params.set(CN_ADDRESS, "0.0.0.0");
    listener_params.set(CN_PORT, "3306");
    listener_params.set(CN_PROTOCOL, "mariadbclient");
    listener_params.set(CN_SERVICE, service->name());

    auto listener = Listener::create("listener", "mariadbclient", listener_params);

    auto session = new mxs::Session(listener);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    mxb_assert(fd >= 0);

    std::unique_ptr<mxs::ClientProtocol> client_protocol(MySQLClientProtocol::create(session, session));
    auto dcb = ClientDCB::create(fd, sockaddr_storage {}, session, std::move(client_protocol), nullptr);

    mxb_assert(dcb);
    mxb_assert(dcb->enable_events());
    mxb_assert(dcb->disable_events());
    mxb_assert(dcb->enable_events());


    // This part is pointless as there will be no events for the DCB
    // fprintf(stderr, "\t..done\nStart wait for events.");
    // sleep(10);

    // TODO, fix this for workers: poll_shutdown();
    fprintf(stderr, "\t..done\nTidy up.");
    DCB::close(dcb);
    fprintf(stderr, "\t..done\n");
}

int main(int argc, char** argv)
{
    run_unit_test(test1);
    return 0;
}
