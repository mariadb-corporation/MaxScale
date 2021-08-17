/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
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
#include <string.h>
#include <maxscale/dcb.hh>
#include <maxscale/listener.hh>
#include <maxscale/service.hh>

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
    mxs::ConfigParameters parameters;
    parameters.set(CN_CONNECTION_TIMEOUT, "10s");
    parameters.set(CN_NET_WRITE_TIMEOUT, "10s");
    parameters.set(CN_CONNECTION_KEEPALIVE, "100s");
    parameters.set(CN_USER, "user");
    parameters.set(CN_PASSWORD, "password");
    parameters.set(CN_ROUTER, "readconnroute");
    auto service = Service::create("service", parameters);

    mxs::ConfigParameters listener_params;
    listener_params.set(CN_ADDRESS, "0.0.0.0");
    listener_params.set(CN_PORT, "3306");
    listener_params.set(CN_PROTOCOL, "mariadb");
    listener_params.set(CN_SERVICE, service->name());

    auto listener_data = Listener::create_test_data(listener_params);
    auto session = new Session(listener_data, "127.0.0.1");
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    mxb_assert(fd >= 0);

    auto client_protocol = listener_data->m_proto_module->create_client_protocol(session, session);
    auto pProtocol = client_protocol.get();
    auto dcb = ClientDCB::create(fd, "127.0.0.1", sockaddr_storage {},
                                 session, std::move(client_protocol), mxs::RoutingWorker::get_current());
    pProtocol->set_dcb(dcb);
    session->set_client_connection(pProtocol);

    mxb_assert(dcb);
    mxb_assert(dcb->enable_events());
    mxb_assert(dcb->disable_events());
    mxb_assert(dcb->enable_events());


    // This part is pointless as there will be no events for the DCB
    // fprintf(stderr, "\t..done\nStart wait for events.");
    // sleep(10);

    // TODO, fix this for workers: poll_shutdown();
    fprintf(stderr, "\t..done\nTidy up.");
    ClientDCB::close(dcb);
    fprintf(stderr, "\t..done\n");
}

int main(int argc, char** argv)
{
    run_unit_test(test1);
    return 0;
}
