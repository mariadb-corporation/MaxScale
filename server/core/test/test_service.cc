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

/**
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                 Description
 * 08-09-2014   Martin Brampton     Initial implementation
 *
 * @endverbatim
 */

// To ensure that ss_info_assert asserts also when builing in non-debug mode.
#if !defined(SS_DEBUG)
#define SS_DEBUG
#endif
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxscale/maxscale_test.h>
#include <maxscale/paths.h>
#include <maxscale/alloc.h>

#include "../internal/service.hh"
#include "test_utils.h"
#include "../config.cc"

/**
 * test1    Allocate a service and do lots of other things
 *
  */
static int
test1()
{
    Service     *service;
    MXS_SESSION *session;
    DCB         *dcb;
    int         result;
    int         argc = 3;

    mxs_log_init(NULL, "/tmp", MXS_LOG_TARGET_FS);
    init_test_env(NULL);

    set_libdir(MXS_STRDUP_A("../../modules/authenticator/MySQLAuth/"));
    load_module("mysqlauth", MODULE_AUTHENTICATOR);
    set_libdir(MXS_STRDUP_A("../../modules/protocol/MySQL/mariadbclient/"));
    load_module("mariadbclient", MODULE_PROTOCOL);
    set_libdir(MXS_STRDUP_A("../../modules/routing/readconnroute/"));
    load_module("readconnroute", MODULE_ROUTER);

    /* Service tests */
    ss_dfprintf(stderr,
                "testservice : creating service called MyService with router nonexistent");
    service = service_alloc("MyService", "non-existent", NULL);
    ss_info_dassert(NULL == service, "New service with invalid router should be null");
    ss_info_dassert(0 == service_isvalid(service), "Service must not be valid after incorrect creation");
    ss_dfprintf(stderr, "\t..done\nValid service creation, router testroute.");
    service = service_alloc("MyService", "readconnroute", NULL);

    ss_info_dassert(NULL != service, "New service with valid router must not be null");
    ss_info_dassert(0 != service_isvalid(service), "Service must be valid after creation");
    ss_info_dassert(0 == strcmp("MyService", service->name), "Service must have given name");
    ss_dfprintf(stderr, "\t..done\nAdding protocol testprotocol.");
    ss_info_dassert(serviceCreateListener(service, "TestProtocol", "mariadbclient",
                                          "localhost", 9876, "MySQLAuth", NULL, NULL),
                    "Add Protocol should succeed");
    ss_info_dassert(0 != serviceHasListener(service, "TestProtocol", "mariadbclient", "localhost", 9876),
                    "Service should have new protocol as requested");

    return 0;

}

int main(int argc, char **argv)
{
    int result = 0;

    result += test1();

    exit(result);
}
