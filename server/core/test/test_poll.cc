/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-12
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
#include "../../modules/protocol/MariaDB/protocol_module.hh"

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
    parameters.set(CN_CONNECTION_KEEPALIVE, "100s");
    auto service = Service::create("service", "readconnroute", &parameters);

    MXS_CONFIG_PARAMETER listener_params;
    listener_params.set(CN_ADDRESS, "0.0.0.0");
    listener_params.set(CN_PORT, "3306");
    listener_params.set(CN_PROTOCOL, "mariadbclient");
    listener_params.set(CN_SERVICE, service->name());

    auto listener = Listener::create("listener", "mariadbclient", listener_params);

    std::shared_ptr<mxs::ProtocolModule> protocol_module(MySQLProtocolModule::create("", ""));
    auto session = new Session(protocol_module, listener->shared_data(), "127.0.0.1");
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    mxb_assert(fd >= 0);


    auto client_protocol = protocol_module->create_client_protocol(session, session);
    auto pProtocol = client_protocol.get();
    auto dcb = ClientDCB::create(fd, "127.0.0.1", sockaddr_storage {},
                                 session, std::move(client_protocol), nullptr);
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
    DCB::close(dcb);
    fprintf(stderr, "\t..done\n");
}

int main(int argc, char** argv)
{
    run_unit_test(test1);
    return 0;
}
